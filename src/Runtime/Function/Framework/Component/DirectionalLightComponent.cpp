
#include "DirectionalLightComponent.h"
#include "Core/Math/Math.h"
#include "Function/Framework/Component/CameraComponent.h"
#include "Function/Framework/Component/TransformComponent.h"
#include "Function/Framework/Scene/Scene.h"
#include "Function/Global/Definations.h"
#include "Function/Global/EngineContext.h"
#include "TryGetComponent.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <ostream>

CEREAL_REGISTER_TYPE(DirectionalLightComponent)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, DirectionalLightComponent)

void DirectionalLightComponent::OnInit()
{
	Component::OnInit();
}

void DirectionalLightComponent::OnUpdate(float deltaTime)
{
	InitComponentIfNeed();
}

void DirectionalLightComponent::UpdateMatrix()
{
    std::shared_ptr<TransformComponent> transform = TryGetComponent<TransformComponent>();
    if(transform)
    {
        front = transform->Front();
        up = transform->Up();
    }
    else
    {
    	front = Vec3::UnitX();
	    up = Vec3::UnitY();
    }
}

void DirectionalLightComponent::UpdateLightInfo()
{
    UpdateMatrix();

    lightInfo.color = color;
    lightInfo.intencity = intencity;
    lightInfo.fogScattering = fogScattering;
    lightInfo.castShadow = castShadow;
    lightInfo.dir = front.normalized();
    lightInfo.lightID = 0;    // ：方向光暂无id池，恒0（mainDirection_id.w占位）

    EngineContext::RenderResource()->SetDirectionalLightInfo(lightInfo);
}
