#include "RenderSurfaceCacheManager.h"
#include "Core/Event/AllEvents.h"
#include "Core/Event/Event.h"
#include "Core/Math/BoundingBox.h"
#include "Core/Math/Math.h"
#include "Core/SurfaceCache/SurfaceCache.h"
#include "Function/Global/Definations.h"
#include "Function/Global/EngineContext.h"
#include "Function/Framework/Component/MeshRendererComponent.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderPass/RenderPass.h"
#include "Function/Render/RenderResource/RenderStructs.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <minwindef.h>
#include <vector>

void RenderSurfaceCacheManager::Init()
{
    cache = std::vector<MeshCardCache>(MAX_PER_FRAME_OBJECT_SIZE * 6 + 1); 
    cards = std::vector<MeshCardInfo>(MAX_PER_FRAME_OBJECT_SIZE * 6 + 1); 
    sortedID = std::vector<uint32_t>(MAX_PER_FRAME_OBJECT_SIZE * 6 + 1);
    std::iota(sortedID.begin(), sortedID.end(), 0);

    // 注册材质更新事件，在材质更新后要同时强制更新表面缓存
    materialUpdateEvent = EngineContext::Event()->AddListener([this] (const std::shared_ptr<Event>& event){
        auto eve = std::dynamic_pointer_cast<MaterialUpdateEvent>(event);
        this->materialUpdate[eve->material->GetMaterialID()] = true;
    }, EVENT_MATERIAL_UPDATE);
}

void RenderSurfaceCacheManager::Tick()
{
    UpdateSurfaceCache();

    for(auto& pair : materialUpdate)    // 清空材质更新状况
        pair.second = false;
}

const std::vector<SurfaceCacheRasterizeDraw>& RenderSurfaceCacheManager::GetRasterizeDraws()               
{ 
    return perFrameTasks[EngineContext::ThreadPool()->ThreadFrameIndex()].rasterizeDraws; 
}

const std::vector<SurfaceCacheLightingDispatch>& RenderSurfaceCacheManager::GetDirectLightingDispatches()  
{ 
    return perFrameTasks[EngineContext::ThreadPool()->ThreadFrameIndex()].directLightingDispatches; 
}

const std::vector<Rect2D>& RenderSurfaceCacheManager::GetClearScissors()                                   
{ 
    return perFrameTasks[EngineContext::ThreadPool()->ThreadFrameIndex()].clearScissors; 
}

void RenderSurfaceCacheManager::ReleaseCache(uint32_t meshCardID)
{
    if(cache[meshCardID].valid)
    {
        objectIDtoCardID.erase(cache[meshCardID].objectID);
        for(int face = 0; face < 6; face++)
        {
            auto& entry = cache[meshCardID + face];
            if(entry.atlasRange) atlas.Release(entry.atlasRange); // 释放cache

            cache[meshCardID + face].valid = false;
        }
    }
    if(maxUsedMeshCardID == meshCardID) 
    {
        maxUsedMeshCardID -= 6;
        std::iota(sortedID.begin(), sortedID.end(), 0);     // 如果最大的已使用meshCardID范围变化，需要重新初始化一次排序下标
    }                                                                        // 否则就可以保持排序状态，在下一帧排序时已经部分有序？不确定是否有效
}

void RenderSurfaceCacheManager::InitCache(const SurfaceCacheTask& task)
{
    objectIDtoCardID[task.objectID] = task.meshCardID;
    maxUsedMeshCardID = std::max(maxUsedMeshCardID, task.meshCardID);

    Vec3 extent = task.box.maxBound - task.box.minBound;
    for(int i = 0; i < 3; i++) extent[i] = std::max(extent[i], 0.05f);  // 取个下限避免面片的问题

    Vec3 center = (task.box.maxBound + task.box.minBound) / 2;
    extent = extent.array() * task.scale.array();   // 按实际尺寸来计算
    center = center.array() * task.scale.array(); 

    static const Vec3 direction[6] = {
        Vec3::UnitX(),
        -Vec3::UnitX(),
        Vec3::UnitY(),
        -Vec3::UnitY(),
        Vec3::UnitZ(),
        -Vec3::UnitZ(),
    };

    static const Vec3 up[3] = {
        Vec3::UnitY(),
        Vec3::UnitX(),
        Vec3::UnitY(),
    };
    static const Vec3 rotateUp[3] = {
        Vec3::UnitZ(),
        Vec3::UnitZ(),
        Vec3::UnitX(),
    };
    static const IVec3 axis[3] = {      //view space:       x正向向右，y正向向上，z负向向前
        IVec3(2, 1, 0),
        IVec3(2, 0, 1),
        IVec3(0, 1, 2),
    };
    static const IVec3 rotateAxis[3] = {
        IVec3(1, 2, 0),
        IVec3(0, 2, 1),
        IVec3(1, 0, 2),
    };

    for(int face = 0; face < 6; face++)  // 初始化每个card的视角信息，前后上下右左
    {
        // 该card视角方向下extent的宽小于高，旋转对应的up方向
        bool rotate =   ((face == 0 || face == 1) && (extent.z() < extent.y())) ||
                        ((face == 2 || face == 3) && (extent.z() < extent.x())) ||
                        ((face == 4 || face == 5) && (extent.x() < extent.y()));
        IVec3 viewAxis = rotate ? rotateAxis[face / 2] : axis[face / 2];
        // 相机看向包围盒中心，所以 viewDirection 是反方向
        Vec3 viewDirection  = -direction[face];
        Vec3 viewUp         = rotate ? rotateUp[face / 2] : up[face / 2];

        MeshCardCache entry = {};
        entry.valid = true;
        entry.objectID = task.objectID;
        auto& card = cards[task.meshCardID + face];
        card.viewPosition   = center.array() + extent.array() / 2 * direction[face].array();
        card.viewExtent     = Vec3(extent[viewAxis.x()], extent[viewAxis.y()], extent[viewAxis.z()]);
        card.scale          = task.scale;
        card.view           = Math::LookAt(card.viewPosition, center, viewUp);
        card.proj           = Math::Ortho(card.viewExtent.x() / 2, -card.viewExtent.x() / 2, 
                                        card.viewExtent.y() / 2, -card.viewExtent.y() / 2, 
                                        0.0, card.viewExtent.z());
        card.proj(1, 1) *= -1;		// Vulkan的NDC是y向下                                
        //card.proj = Math::Perspective(Math::ToRadians(90.0f), 1.0f, 0.01f, card.viewExtent.z());
        card.viewProj = card.proj * card.view;
        card.invView = card.view.inverse();
        card.invProj = card.proj.inverse();

        cache[task.meshCardID + face] = entry;
    }
}

void RenderSurfaceCacheManager::UpdateSurfaceCache()
{
    if(!dynamicUpdate) return;

    ENGINE_TIME_SCOPE(RenderSurfaceCacheManager::UpdateSurfaceCache);

    auto& perFrameTask = perFrameTasks[EngineContext::ThreadPool()->ThreadFrameIndex()];
    perFrameTask.rasterizeDraws.clear();
    perFrameTask.clearScissors.clear();
    perFrameTask.directLightingDispatches.clear();

    if( !EngineContext::Render()->IsPassEnabled(RESTIR_GI_PASS) &&
        !EngineContext::Render()->IsPassEnabled(SSSR_PASS) &&  
        !EngineContext::Render()->IsPassEnabled(RAY_TRACING_BASE_PASS))
        return; // 更新的CPU侧开销很大(没优化)，渲染没用上就不更新了

    auto camera = EngineContext::World()->GetActiveScene()->GetActiveCamera();
    Vec3 cameraPos = camera->GetPosition();
    UVec2 padding = UVec2(SURFACE_CACHE_PADDING, SURFACE_CACHE_PADDING);

    // 遍历场景，获取绘制信息
    // TODO 场景的CPU端剔除
    std::vector<SurfaceCacheTask> tasks;
    {
        ENGINE_TIME_SCOPE(RenderSurfaceCacheManager::CollectTask);
        auto rendererComponents = EngineContext::World()->GetActiveScene()->GetComponents<MeshRendererComponent>();     // 场景物体
        for(auto& component : rendererComponents) component->CollectSurfaceCacheTask(tasks);
    }

    // 在这里完成每帧内surface cache的主要操作，包括
    // 为新card分配空间、动态更新每个card所占cache分辨率、确定光栅化和光照计算的范围等

    // 物体根据其世界空间尺寸以及在世界空间与相机的距离确认分辨率
    {
        ENGINE_TIME_SCOPE(RenderSurfaceCacheManager::Rasterize);

        // 光栅化预算
        int32_t rasterizeBuget = MAX_SURFACE_CACHE_RASTERIZE_SIZE * MAX_SURFACE_CACHE_RASTERIZE_SIZE;
        for(auto& task : tasks) 
        {
            task.forceUpdate |= materialUpdate[task.materialID];    // 材质发生更新，强制处理
            if(!task.forceUpdate && rasterizeBuget <= 0) break;

            if(objectIDtoCardID.contains(task.objectID) && 
                objectIDtoCardID[task.objectID] != task.meshCardID) 
            {
                ReleaseCache(task.meshCardID);                              // ID改变？ TODO 
                objectIDtoCardID.erase(task.objectID);
            }

            bool initCache = !objectIDtoCardID.contains(task.objectID);
            if(initCache) InitCache(task);                                              // 首次提交的数据，先初始化
                                                             
            for(int face = 0; face < 6; face++)
            {
                auto& entry = cache[task.meshCardID + face];
                auto& card = cards[task.meshCardID + face];
                auto& range = entry.atlasRange;
                auto& prevRange = entry.prevAtlasRange;

                //if(!dynamicUpdate && entry.rasterized) continue;
                
                // 1. 分配cache空间    
                Vec3 center = task.pos;
                float radious = task.sphere.radius * task.scale.norm();
                float distance = (cameraPos - center).norm() - radious;
                int32_t scaleFactor = fmax(5.0f - fmax(distance, 0.0f) / 10.0f, 0.0f);  // 指数，每10米衰减一半
                if(fixScale) scaleFactor = 4;

                bool reallocate = range && !prevRange && 
                    (entry.scaleFactor != scaleFactor || task.forceUpdate); // 尺寸发生变化或强制更新，且未进入更新流程

                if(!entry.valid) continue;
                if(!range || reallocate)
                {
                    float scale = pow(2, scaleFactor);
                    UVec2 paddedExtent = UVec2(card.viewExtent.x() * scale, 
                                            card.viewExtent.y() * scale);          // 尺寸暂时就这样？ TODO
                    //if(paddedExtent.x() * paddedExtent.y() < 512.0)  paddedExtent *= 4;         // 适当扩大一点小物体的？   TODO
                    paddedExtent += padding;

                    auto newRange = atlas.Allocate(paddedExtent);       
                    if(newRange) 
                    {
                        if(reallocate)   
                        {
                            //atlas.Release(range);     // 存储旧范围，等到新范围进行过光照计算后释放
                            prevRange = range;
                            range = newRange;

                            // 保存为旧的采样位置
                            card.sampleAtlasOffset = prevRange->offset + padding;
                            card.sampleAtlasExtent = prevRange->extent - padding;
                        }
                        else 
                        {
                            prevRange = nullptr;
                            range = newRange;

                            card.sampleAtlasOffset = range->offset + padding;
                            card.sampleAtlasExtent = range->extent - padding;
                        }

                        // 光栅化指向新空间
                        card.atlasOffset = range->offset + padding;
                        card.atlasExtent = range->extent - padding;
                        entry.directLightings.clear();  // 清空光照相关数据
                        entry.lastUpdateTick = 0;       // 标记为最久未更新
                        entry.rasterized = false;       // 标记未光栅

                        entry.scaleFactor = scaleFactor;
                    }
                } 
                else continue;  // 无需进行重新光栅，跳过

                // 2. 处理光栅化任务
                if(!range) continue;
                uint32_t size = range->AllocatedSize();
                {
                    rasterizeBuget -= task.forceUpdate ? 0 : size;  // 强制更新的就不算buget了？
                    entry.rasterized = true;

                    perFrameTask.rasterizeDraws.emplace_back();
                    perFrameTask.rasterizeDraws.back().task = task;
                    perFrameTask.rasterizeDraws.back().task.meshCardID = task.meshCardID + face;  
                    perFrameTask.rasterizeDraws.back().atlasOffset = range->offset + padding;
                    perFrameTask.rasterizeDraws.back().atlasExtent = range->extent - padding;    
                }
            }
        }
    }

    // 3. 处理光照计算任务
    // UE使用的是以每128*128的page为基本单位进行更新，这里直接用的是card为单位 
    {
        ENGINE_TIME_SCOPE(RenderSurfaceCacheManager::SortCache);

        EngineContext::RenderResource()->GetMeshCardReadback(readback); // 根据回读信息来做优先级队列
        std::sort(sortedID.begin() + 1,                             // 响应速度会快不少，UE是GPU radix sort实现的
                sortedID.begin() + maxUsedMeshCardID + 6 + 1,        // 回读很快，但是CPU排序太慢了
                [&](const int32_t& a, const int32_t& b) {
            if(!cache[a].valid) return false;
            if(!cache[b].valid) return true;
            return  (readback[a] - cache[a].lastUpdateTick) >
                    (readback[b] - cache[b].lastUpdateTick);   // lastUsed - lastUpdated
        });
    }

    {
        ENGINE_TIME_SCOPE(RenderSurfaceCacheManager::Lighting);

        uint32_t directLightingBuget = MAX_SURFACE_CACHE_DIRECT_LIGHTING_SIZE * MAX_SURFACE_CACHE_DIRECT_LIGHTING_SIZE;
        for(int i = 1; i < maxUsedMeshCardID + 6 + 1; i++)
        {
            uint32_t meashCardID = sortedID[i];
            auto& entry = cache[meashCardID];
            auto& card = cards[meashCardID];
            if(!entry.valid) continue;

            // 预算不足一个 tile，直接退出
            if(directLightingBuget < SURFACE_CACHE_DIRECT_LIGHTING_TILE_SIZE * SURFACE_CACHE_DIRECT_LIGHTING_TILE_SIZE) break;
            {
                auto& range = entry.atlasRange;
                if(!range) continue;
                if(!entry.rasterized) continue;
                //if(EngineContext::GetCurretTick() - entry.lastUpdateTick < 30) continue; // 也可以简单的遍历，对超过固定帧数更新一次

                uint32_t size = range->AllocatedSize();
                if(directLightingBuget > size)
                {
                    directLightingBuget -= size;
                    entry.lastUpdateTick = EngineContext::GetCurretTick();

                    if(entry.prevAtlasRange)                    // 有旧范围数据，在新范围进行首次光照计算后就可释放了
                    {
                        Rect2D scissor = {};
                        scissor.offset.x = (card.sampleAtlasOffset - padding).x();
                        scissor.offset.y = (card.sampleAtlasOffset - padding).y();
                        scissor.extent.width = (card.sampleAtlasExtent + padding).x();
                        scissor.extent.height = (card.sampleAtlasExtent + padding).y();
                        perFrameTask.clearScissors.push_back(scissor);  // 旧范围的GBuffer清理

                        atlas.Release(entry.prevAtlasRange);
                        entry.prevAtlasRange = nullptr;

                        card.sampleAtlasOffset = range->offset + padding;
                        card.sampleAtlasExtent = range->extent - padding;

                        //EngineContext::RenderResource()->SetMeshCardInfo(card, meashCardID);  
                    }

                    if(entry.directLightings.size() == 0)
                    {
                        UVec2 tiledOffset = entry.atlasRange->TiledOffset(SURFACE_CACHE_DIRECT_LIGHTING_TILE_SIZE);
                        UVec2 tiledExtent = entry.atlasRange->TiledExtent(SURFACE_CACHE_DIRECT_LIGHTING_TILE_SIZE);
                        for(int col = 0; col < tiledExtent.x(); col++)
                        {
                            for(int row = 0; row < tiledExtent.y(); row++)
                            {
                                entry.directLightings.emplace_back();
                                entry.directLightings.back().meshCardID = sortedID[i]; 
                                entry.directLightings.back().objectID = entry.objectID;
                                entry.directLightings.back().tileOffset = tiledOffset;
                                entry.directLightings.back().tileIndex = UVec2(col, row);   
                            }
                        }
                    }
                    perFrameTask.directLightingDispatches.insert(perFrameTask.directLightingDispatches.end(), 
                                                    entry.directLightings.begin(), 
                                                    entry.directLightings.end());
                }
            }
        }
    }

    EngineContext::RenderResource()->SetMeshCardInfos(cards, maxUsedMeshCardID + 6 + 1);  
}