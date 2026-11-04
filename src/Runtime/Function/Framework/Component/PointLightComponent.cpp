
#include "PointLightComponent.h"
#include "Core/Math/Math.h"
#include "Function/Framework/Component/TransformComponent.h"
#include "Function/Global/EngineContext.h"
#include "TryGetComponent.h"

CEREAL_REGISTER_TYPE(PointLightComponent)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, PointLightComponent)

PointLightComponent::~PointLightComponent()
{
    if(!EngineContext::Destroyed() && pointLightID != 0) EngineContext::RenderResource()->ReleasePointLightID(pointLightID);
}

void PointLightComponent::OnInit()
{
    Component::OnInit();

    pointLightID = EngineContext::RenderResource()->AllocatePointLightID();
}

void PointLightComponent::OnUpdate(float deltaTime)
{
    InitComponentIfNeed();

	//UpdateLightInfo();
}

void PointLightComponent::UpdateLightInfo(uint32_t gpuSlot)
{
    {
        Vec3 pos = Vec3::Zero();

        std::shared_ptr<TransformComponent> transform = TryGetComponent<TransformComponent>();
        if(transform)
        {
            //更新包围信息
            pos = transform->GetPosition();

            //box = BoundingBox(pos - Vec3::Constant(far), pos + Vec3::Constant(far));
            sphere = BoundingSphere(pos, far);
        }

        //无阴影点光源信息
        {
            info.pos = pos;
            info.color = color;
            info.intencity = intencity;
            info.fogScattering = fogScattering;
            info.far = far;
            info.castShadow = CastShadow() ? 1 : 0;
            info.lightID = pointLightID;    
        }

    }

    EngineContext::RenderResource()->SetPointLightInfo(info, gpuSlot);
}


