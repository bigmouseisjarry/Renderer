#pragma once

#include "Component.h"
#include "Core/Math/Math.h"
#include "Core/Math/Transform.h"

class TransformComponent : public Component
{
public:
	TransformComponent() = default;
	~TransformComponent() {};
	
	virtual void OnInit() override;
	virtual void OnUpdate(float deltaTime) override;

	inline void SetTransform(Mat4 mat) 					{ transform = Transform(mat); }
	inline void SetPosition(Vec3 position) 				{ transform.SetPosition(position); }
	inline void SetScale(Vec3 scale) 					{ transform.SetScale(scale); }
	inline void SetRotation(Quaternion rotation) 		{ transform.SetRotation(rotation); }
	inline void SetRotation(Vec3 angle) 				{ transform.SetRotation(angle); }
    Vec3 Translate(Vec3 translation)        			{ return transform.Translate(translation); }	// TODO 相对操作
    Vec3 Scale(Vec3 scale)                  			{ return transform.Scale(scale); }
    Vec3 Rotate(Vec3 angle)                 			{ return transform.Rotate(angle); }

	inline Vec3 Front() const							{ return transform.Front(); }
    inline Vec3 Up() const                  			{ return transform.Up(); }
    inline Vec3 Right() const               			{ return transform.Right(); }

    inline Vec3 GetPosition() const         			{ return transform.GetPosition(); }
    inline Vec3 GetScale() const            			{ return transform.GetScale(); }
	inline Quaternion GetRotation() const   			{ return transform.GetRotation(); }
	inline Vec3 GetEulerAngle() const					{ return transform.GetEulerAngle(); }

	Mat4 GetModelMat()									{ return transform.GetMatrix(); }
	Mat4 GetModelMatInv()								{ return transform.GetMatrix().inverse(); }

	virtual std::string GetTypeName() override			{ return "Transform Component"; }
	virtual ComponentType GetType()	override final		{ return TRANSFORM_COMPONENT; }

private:
    Transform transform;

	void UpdateMatrix();

private:
    BeginSerailize()
    SerailizeBaseClass(Component)
    SerailizeEntry(transform)
    EndSerailize

	EnableComponentEditourUI()
};