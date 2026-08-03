#pragma once

#include "Component.h"
#include "Core/Math/BoundingBox.h"
#include "Core/Math/Math.h"
#include "Core/Serialize/Serializable.h"
#include "Function/Render/RenderResource/RenderStructs.h"

class CameraComponent : public Component
{
public:
	CameraComponent() {};
	~CameraComponent() {};

	virtual void OnInit() override;
	virtual void OnUpdate(float deltaTime) override;

	bool IsActiveCamera();
	bool IsMoved() 											{ return move; }

	inline void SetFov(float fovy) 							{ this->fovy = fovy; }

	inline Vec3 GetPosition() const							{ return position; }
	inline Vec3 GetFront() const							{ return front; }
	inline Vec3 GetUp() const								{ return right.cross(front).normalized(); }	
	inline Vec3 GetRight() const							{ return right; }
	inline float GetNear() const							{ return near; }
	inline float GetFar() const								{ return far; }
	inline float GetFov() const								{ return fovy; }
	inline float GetAspect() const							{ return aspect; }

	inline Mat4 GetViewMatrix() const						{ return view; }
	inline Mat4 GetProjectionMatrix() const					{ return proj; }
	inline Mat4 GetPrevViewMatrix() const					{ return prevView; }
	inline Mat4 GetPrevProjectionMatrix() const				{ return prevProj; }
	inline Mat4 GetInvViewMatrix() const					{ return invView; }
	inline Mat4 GetInvProjectionMatrix() const				{ return invProj; }
	inline Mat4 GetViewProjectionMatrix() const				{ return viewProj; }
	inline Mat4 GetInvViewProjectionMatrix() const			{ return invViewProj; }

	Frustum GetFrustum()									{ return frustum; }

	virtual std::string GetTypeName() override				{ return "Camera Component"; }
	virtual ComponentType GetType()	override 				{ return CAMERA_COMPONENT; }

	void UpdateCameraInfo();

private:
	// float yaw = 0.0f;
	// float pitch = 0.0f;
	float fovy = 90.0f;
    float aspect = 16.0f / 9.0f;    // TODO 
	float near = 0.2f;
	float far = 1000.0f;

	Vec3 position = Vec3::Zero();
	Vec3 front = Vec3::Zero();
	Vec3 up = Vec3::Zero();
	Vec3 right = Vec3::Zero();

	Mat4 view;
	Mat4 proj;
	Mat4 viewProj;
	Mat4 prevView;	//前一帧的view
	Mat4 prevProj;	//前一帧的proj
	Mat4 invView;
	Mat4 invProj;
	Mat4 invViewProj;

    Frustum frustum;

	CameraInfo cameraInfo;
	bool move = false;

	void InputMove(float deltaTime);

	void UpdateMatrix();

private:
    BeginSerailize()
    SerailizeBaseClass(Component)
	// SerailizeEntry(yaw)
    // SerailizeEntry(pitch)
    SerailizeEntry(fovy)
	SerailizeEntry(aspect)
    SerailizeEntry(near)
	SerailizeEntry(far)
    EndSerailize

	EnableComponentEditourUI()
};

