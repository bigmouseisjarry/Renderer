#include "TLASUpdatePass.h"

#include "Function/Global/EngineContext.h"
#include "Function/Render/RenderSystem/RenderSystem.h"

void TLASUpdatePass::Build(RDGBuilder& builder)
{
    if(!IsEnabled()) return;    // 禁用时RT pass的Dependency按无效句柄跳过，读到的将是上一帧的TLAS

    auto meshManager = EngineContext::Render()->GetMeshManager();
    uint32_t slot = EngineContext::CurrentFrameIndex();     // Build期在主线程，帧槽已定

    auto storage = builder.GetOrCreateBuffer("TLAS Storage")
        .Import(meshManager->GetTLASStorageBuffer(slot), RESOURCE_STATE_ACCELERATION_STRUCTURE)
        .Finish();

    auto srcStorage = builder.GetOrCreateBuffer("TLAS Storage Src")
        .Import(meshManager->GetTLASStorageBuffer(slot ^ 1), RESOURCE_STATE_ACCELERATION_STRUCTURE)
        .Finish();

    auto tlas = meshManager->GetTLAS(slot);
    auto instances = meshManager->GetInstances();     // 按值捕获：Execute期并行录制安全；manager的instances下帧即被worker重写

    builder.CreateRayTracingPass("TLAS Update")
        .Write(storage, RESOURCE_STATE_ACCELERATION_STRUCTURE)          
        .ReadAccelerationStructure(srcStorage)
        .Execute([tlas, instances](RDGPassContext context)
        {
            // 一步构建：instance写入+乒乓mode推导（count门控降级BUILD）+屏障录制，全在命令流内完成
            context.command->BuildTopLevelAccelerationStructure(tlas, instances);
        });
}
