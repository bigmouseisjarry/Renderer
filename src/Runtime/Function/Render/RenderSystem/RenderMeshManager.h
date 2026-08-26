#pragma once

#include "Function/Render/RHI/RHIResource.h"
class RenderMeshManager
{
public:
    void Init();
    void Tick();

    // TLAS的CPU侧准备（instance写入+构建信息填充），由RDG的TLASUpdatePass在Build期调用；
    // 构建命令的录制由该pass在帧命令流内完成（原独立提交的UpdateTLAS已收编进图）
    void PrepareTLASUpdate();

    RHITopLevelAccelerationStructureRef GetTLAS()            { return tlas; }
    RHIBufferRef GetTLASStorageBuffer()                      { return tlas->GetStorageBuffer(); }

private:
    void PrepareMeshPass();
    void PrepareRayTracePass();

    std::vector<RHIAccelerationStructureInstanceInfo> instances;
    RHITopLevelAccelerationStructureRef tlas;
    bool init = false;
};