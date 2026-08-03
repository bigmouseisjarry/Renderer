#pragma once

#include "Function/Framework/Component/DirectionalLightComponent.h"
#include "Function/Framework/Component/PointLightComponent.h"
#include "Function/Framework/Component/VolumeLightComponent.h"
#include "Function/Global/Definations.h"
#include <array>
#include <memory>
#include <vector>
class RenderLightManager
{
public:
    void Init();
    void Tick();

    std::shared_ptr<DirectionalLightComponent> GetDirectionalLight();
    const std::vector<std::shared_ptr<PointLightComponent>>& GetPointShadowLights();
    const std::vector<std::shared_ptr<VolumeLightComponent>>& GetVolumeLights();

private:
    void PrepareLights();

    struct PerFrameLights
    {
        std::vector<std::shared_ptr<PointLightComponent>> pointShadowLights;
        std::shared_ptr<DirectionalLightComponent> directionalLight;
        std::vector<std::shared_ptr<VolumeLightComponent>> volumeLights;
    };
    std::array<PerFrameLights, FRAMES_IN_FLIGHT> perframeLights;
    
};