#include "CameraComponent.h"
#include "Component.h"
#include "Core/Math/Math.h"
#include "Core/Math/Transform.h"
#include "Function/Framework/Component/TransformComponent.h"
#include "Function/Framework/Entity/Entity.h"
#include "Function/Global/EngineContext.h"
#include "Platform/Input/InputSystem.h"
#include "TryGetComponent.h"
#include <memory>

CEREAL_REGISTER_TYPE(CameraComponent)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, CameraComponent)

void CameraComponent::OnInit()
{
	Component::OnInit();
	
	UpdateMatrix();
}

void CameraComponent::OnUpdate(float deltaTime)
{
	InitComponentIfNeed();

	if(IsActiveCamera()) InputMove(deltaTime);
	UpdateMatrix();
}

void CameraComponent::InputMove(float deltaTime)
{
	std::shared_ptr<TransformComponent> transformComponent = TryGetComponent<TransformComponent>();
	if(!transformComponent) return;

    float speed = 5.0f;
    float delta = speed * deltaTime / 1000.0f;
	float sensitivity = 0.5f;

	// 位移
    Vec3 deltaPosition = Vec3::Zero();
	if(EngineContext::Input()->KeyIsPressed(KEY_TYPE_LEFT_SHIFT))		delta *= 5;
	if(EngineContext::Input()->KeyIsPressed(KEY_TYPE_W))				deltaPosition += transformComponent->Front() * delta;
	if(EngineContext::Input()->KeyIsPressed(KEY_TYPE_S))				deltaPosition -= transformComponent->Front() * delta;
	if(EngineContext::Input()->KeyIsPressed(KEY_TYPE_A))				deltaPosition -= transformComponent->Right() * delta;
	if(EngineContext::Input()->KeyIsPressed(KEY_TYPE_D))				deltaPosition += transformComponent->Right() * delta;
	if(EngineContext::Input()->KeyIsPressed(KEY_TYPE_SPACE))			deltaPosition += transformComponent->Up() * delta;
	if(EngineContext::Input()->KeyIsPressed(KEY_TYPE_LEFT_CONTROL))	deltaPosition -= transformComponent->Up() * delta;
    transformComponent->Translate(deltaPosition);

	if (EngineContext::Input()->MouseButtonIsPressed(MOUSE_BUTTON_TYPE_RIGHT)) 
	{
		// 朝向
		Vec2 offset = -EngineContext::Input()->GetMouseDeltaPosition() * sensitivity;

		Vec3 eulerAngle = transformComponent->GetEulerAngle();
		eulerAngle = Math::ClampEulerAngle(eulerAngle + Vec3(0.0f, offset.x(), offset.y()));
		transformComponent->SetRotation(eulerAngle);

		// FOV
		fovy -= EngineContext::Input()->GetScrollDeltaPosition().y() * sensitivity * 2;
		fovy = fovy > 135 ? 135 : fovy;
		fovy = fovy < 30 ? 30 : fovy;
	}
}

bool CameraComponent::IsActiveCamera()
{
	if(	GetEntity() && 
		GetEntity()->GetScene() && 
		GetEntity()->GetScene()->GetActiveCamera().get() == this) return true;

	return false;
}

void CameraComponent::UpdateMatrix()
{
	std::shared_ptr<TransformComponent> transformComponent = TryGetComponent<TransformComponent>();
	if(transformComponent)
	{
		this->position = transformComponent->GetPosition();
		this->front = transformComponent->Front();
		this->up = transformComponent->Up();
		this->right = transformComponent->Right();
	}

	prevView = view;
	prevProj = proj;

	view = Math::LookAt(position, position + front, up);
	proj = Math::Perspective(Math::ToRadians(fovy), aspect, near, far);
	proj(1, 1) *= -1;		// Vulkan的NDC是y向下
	viewProj = proj * view;

	invView = view.inverse();
	invProj = proj.inverse();
	invViewProj = viewProj.inverse();

	move = (prevView == view && prevProj == proj) ? false : true;	// 记录相机是否更新

	frustum = CreateFrustumFromMatrix(viewProj, -1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f);

	cameraInfo.view = view;
	cameraInfo.proj = proj;
	cameraInfo.viewProj = viewProj;
	cameraInfo.prevView = prevView;
	cameraInfo.prevProj = prevProj;
	cameraInfo.prevViewProj = prevProj * prevView;
	cameraInfo.invView = invView;
	cameraInfo.invProj = invProj;
	cameraInfo.pos = position;
	cameraInfo.front = front;
	cameraInfo.up = up;
	cameraInfo.right = right;
	cameraInfo.near = near;
	cameraInfo.far = far;
	cameraInfo.fov = Math::ToRadians(fovy);	//传弧度
	cameraInfo.aspect = aspect;
	cameraInfo.frustum = frustum;
}

void CameraComponent::UpdateCameraInfo()
{
	EngineContext::RenderResource()->SetCameraInfo(cameraInfo);
}
