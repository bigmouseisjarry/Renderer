#pragma once

#include "Function/Global/Definations.h"
#include "Function/Render/RHI/RHIResource.h"
#include <array>

class RenderMeshManager
{
public:
    void Init();
    void Tick();

    // TLAS按帧槽乒乓持有（槽i构建dst=槽i、UPDATE的src=对端槽i^1）；句柄永不变，描述符Init期一次写死
    RHITopLevelAccelerationStructureRef GetTLAS(uint32_t slot)            { return tlas[slot]; }
    RHIBufferRef GetTLASStorageBuffer(uint32_t slot)                      { return tlas[slot]->GetStorageBuffer(); }

    // 本帧收集的instance数据（TLASUpdatePass在Build期取副本按值捕入Execute）
    const std::vector<RHIAccelerationStructureInstanceInfo>& GetInstances() const { return instances; }

private:
    void PrepareMeshPass();
    void PrepareRayTracePass();

    std::vector<RHIAccelerationStructureInstanceInfo> instances;
    std::array<RHITopLevelAccelerationStructureRef, FRAMES_IN_FLIGHT> tlas;
};
