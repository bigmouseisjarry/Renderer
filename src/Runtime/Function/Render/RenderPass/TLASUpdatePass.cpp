#include "TLASUpdatePass.h"

#include "Function/Global/EngineContext.h"
#include "Function/Render/RenderSystem/RenderSystem.h"

void TLASUpdatePass::Build(RDGBuilder& builder)
{
    if(!IsEnabled()) return;    // 禁用时RT pass的Dependency按无效句柄跳过，读到的将是上一帧的TLAS

    auto meshManager = EngineContext::Render()->GetMeshManager();

    // CPU侧准备：instance数据写入持久映射buffer + 构建信息填充。
    // manager的tick在BuildRDG之前已WaitIdle，instances已就绪
    meshManager->PrepareTLASUpdate();

    // 存储buffer以imported身份进图（跨帧持久资源，声明初始状态为AS）
    auto storage = builder.GetOrCreateBuffer("TLAS Storage")
        .Import(meshManager->GetTLASStorageBuffer(), RESOURCE_STATE_ACCELERATION_STRUCTURE)
        .Finish();

    builder.CreateRayTracingPass("TLAS Update")
        .OutputReadWrite(storage)      // 产出边（post-state=UAV）：供RT pass的Dependency边生成 构建写→RT读 的屏障链
        .Execute([meshManager](RDGPassContext context)
        {
            // 构建命令录进帧命令流（首帧BUILD、之后refit，由PrepareUpdate填充的模式决定）
            context.command->BuildTopLevelAccelerationStructure(meshManager->GetTLAS());
        });
}
