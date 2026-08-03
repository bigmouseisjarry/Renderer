#include "RenderMeshManager.h"
#include "Function/Framework/Component/MeshRendererComponent.h"
#include "Function/Global/Definations.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RenderPass/GPUCullingPass.h"
#include "RenderSystem.h"

void RenderMeshManager::Init()
{
    tlas = EngineContext::RHI()->CreateTopLevelAccelerationStructure({
        .maxInstance = MAX_PER_FRAME_OBJECT_SIZE,
        .instanceInfos = {}
    });
    EngineContext::RenderResource()->SetTLAS(tlas);
    init = false;
}

void RenderMeshManager::Tick()
{
    PrepareMeshPass();  

#if ENABLE_RAY_TRACING
    PrepareRayTracePass();
#endif

    std::shared_ptr<CameraComponent> camera = EngineContext::World()->GetActiveScene()->GetActiveCamera();    // TODO 
    if(camera) camera->UpdateCameraInfo();  // 更新相机数据
}

void RenderMeshManager::PrepareMeshPass()
{
    ENGINE_TIME_SCOPE(RenderMeshManager::PrepareMeshPass);

    // 回读上一帧的统计数据
    auto cullingPass = std::dynamic_pointer_cast<GPUCullingPass>(EngineContext::Render()->GetPasses()[GPU_CULLING_PASS]);
    if(cullingPass) cullingPass->CollectStatisticDatas();   

    // 遍历场景，获取绘制信息
    // TODO 场景的CPU端剔除
    std::vector<DrawBatch> batches;
    auto rendererComponents = EngineContext::World()->GetActiveScene()->GetComponents<MeshRendererComponent>();     // 场景物体
    for(auto component : rendererComponents) component->CollectDrawBatch(batches);

    auto skybox = EngineContext::World()->GetActiveScene()->GetSkyBox();    // 天空盒
    if(skybox) skybox->CollectDrawBatch(batches);

    // 交给各个meshpass的processor处理
    MeshPassProcessor::ResetGlobalClusterOffset();
    for(auto& pass : EngineContext::Render()->GetMeshPasses())
    {
        if(!pass) continue;
        pass->GetMeshPassProcessor()->Process(batches);
    }
}

void RenderMeshManager::PrepareRayTracePass()
{
    ENGINE_TIME_SCOPE(RenderMeshManager::PrepareRayTracePass);

    // 遍历场景，获取光追实例信息
    instances.clear();
    auto rendererComponents = EngineContext::World()->GetActiveScene()->GetComponents<MeshRendererComponent>();     // 场景物体
    for(auto component : rendererComponents) component->CollectAccelerationStructureInstance(instances);

    //UpdateTLAS();
}

void RenderMeshManager::UpdateTLAS()
{
    ENGINE_TIME_SCOPE(RenderMeshManager::BuildTLAS);
    
    tlas->Update(instances, !init);
    init = true;
}