#pragma once

#include "Core/Event/EventSystem.h"
#include "Core/Math/BoundingBox.h"
#include "Core/Math/Math.h"
#include "Core/Mesh/Mesh.h"
#include "Core/SurfaceCache/SurfaceCache.h"
#include "Function/Global/Definations.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/Buffer.h"
#include "Function/Render/RenderResource/RenderStructs.h"
#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// 由component提交给manager的绘制SurfaceCache所需信息
struct SurfaceCacheTask 
{
    uint32_t meshCardID;    // 起始值
    uint32_t objectID;
    uint32_t materialID;

    Vec3 scale;
    Vec3 pos;
    BoundingBox box;  
    BoundingSphere sphere;               

    VertexBufferRef vertexBuffer;  
    IndexBufferRef indexBuffer; 

    bool forceUpdate;

    SurfaceCacheTask() = default;
    SurfaceCacheTask(
        uint32_t meshCardID,
        uint32_t objectID,
        uint32_t materialID,
        Vec3 scale,
        Vec3 pos,
        BoundingBox box,      
        BoundingSphere sphere,        
        VertexBufferRef vertexBuffer,
        IndexBufferRef indexBuffer,
        bool forceUpdate = false
    )
    : meshCardID(meshCardID)
    , objectID(objectID)
    , materialID(materialID)
    , scale(scale)
    , pos(pos)
    , box(box)
    , sphere(sphere)
    , vertexBuffer(vertexBuffer)
    , indexBuffer(indexBuffer)
    , forceUpdate(forceUpdate)
    {}
};

// 由manager提交给pass的光栅化所需信息
struct SurfaceCacheRasterizeDraw 
{
    UVec2 atlasOffset = UVec2::Zero();
	UVec2 atlasExtent = UVec2::Zero();

    SurfaceCacheTask task;   // meshCardID是特定face的
};

// 由manager提交给pass的光照所需信息
struct SurfaceCacheLightingDispatch
{
    uint32_t meshCardID;
    uint32_t objectID;
    UVec2 tileOffset = UVec2::Zero();       // 当前tile相对于整个surface cache的偏移
    UVec2 tileIndex = UVec2::Zero();        // 当前tile相对于card的偏移
}; 

class RenderSurfaceCacheManager 
{
public:
    void Init();
    void Tick(); 

    void SetDynamicUpdate(bool dynamicUpdate)   { this->dynamicUpdate = dynamicUpdate; }
    void SetFixScale(bool fixScale)             { this->fixScale = fixScale; }

    const std::vector<SurfaceCacheRasterizeDraw>& GetRasterizeDraws();
    const std::vector<SurfaceCacheLightingDispatch>& GetDirectLightingDispatches();
    const std::vector<Rect2D>& GetClearScissors();

    void ReleaseCache(uint32_t meshCardID);

private:
    void UpdateSurfaceCache();

    void InitCache(const SurfaceCacheTask& task);

    struct MeshCardCache
    {
        bool valid = false;                 // card信息是否有效
        bool rasterized = false;            // card是否已光栅化

        uint32_t objectID = 0;              // 对应的object
        int32_t lastUpdateTick = 0;         // 上次更新光照的帧数
        int32_t scaleFactor = 0;            // 距离缩放系数

        SurfaceAtlasRangeRef atlasRange;    // 在cache上分配的空间信息
        SurfaceAtlasRangeRef prevAtlasRange;            

        std::vector<SurfaceCacheLightingDispatch> directLightings;  // 提交给GPU的直接光照任务
    };
    std::vector<MeshCardInfo> cards;                            // 提交给GPU的card信息
    std::vector<MeshCardCache> cache;                           // 从meshCardID映射到对应的MeshCardCache (现在这样写CPU内存挺浪费的)
    std::unordered_map<uint32_t, uint32_t> objectIDtoCardID;    // 从objectID映射到meshCardID

    MeshCardReadBack readback;              // 回读的采样数信息

    EventHandle* materialUpdateEvent;       // 材质更新
    std::unordered_map<uint32_t, bool> materialUpdate;  
    
    struct PerFrameTasks 
    {
        std::vector<SurfaceCacheRasterizeDraw> rasterizeDraws;                      // 每帧需要进行光栅化的部分
        std::vector<SurfaceCacheLightingDispatch> directLightingDispatches;         // 每帧需要进行直接光照的部分
        std::vector<Rect2D> clearScissors;                                          // 每帧需要清理的部分
    };
    std::array<PerFrameTasks, FRAMES_IN_FLIGHT> perFrameTasks;
    
    SurfaceAtlas atlas;

    uint32_t maxUsedMeshCardID = 0;
    std::vector<uint32_t> sortedID;

    bool dynamicUpdate = true; // TODO 功能没问题，只是大场景的处理太慢了
    bool fixScale = false;
};

