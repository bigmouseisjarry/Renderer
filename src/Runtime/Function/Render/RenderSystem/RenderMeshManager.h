#pragma once

#include "Function/Render/RHI/RHIStructs.h"
class RenderMeshManager
{
public:
    void Init();
    void Tick();

    void UpdateTLAS();

private:
    void PrepareMeshPass();
    void PrepareRayTracePass();

    std::vector<RHIAccelerationStructureInstanceInfo> instances;
    RHITopLevelAccelerationStructureRef tlas;
    bool init = false;
};