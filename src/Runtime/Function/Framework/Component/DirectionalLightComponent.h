#pragma once

#include "Component.h"
#include "Core/Math/BoundingBox.h"
#include "Core/Math/Math.h"
#include "Core/Serialize/Serializable.h"
#include "Function/Global/Definations.h"
#include "Function/Render/RenderResource/RenderStructs.h"

#include <array>
#include <cstdint>

class DirectionalLightComponent : public Component
{
public:
	DirectionalLightComponent() = default;
	~DirectionalLightComponent() {};

	virtual void OnInit() override;
	virtual void OnUpdate(float deltaTime) override;


	virtual std::string GetTypeName() override						{ return "Directional Light Component"; }
	virtual ComponentType GetType() override						{ return DIRECTIONAL_LIGHT_COMPONENT; }


	void SetColor(Vec3 color)										{ this->color = color; } 
	void SetIntencity(float intencity)								{ this->intencity = intencity; } 
	void SetCastShadow(bool castShadow)             				{ this->castShadow = castShadow; }
	void SetFogScattering(float fogScattering)             			{ this->fogScattering = fogScattering; }
	void SetEnable(bool enable)                     				{ this->enable = enable; }

	inline bool CastShadow() const				   			 		{ return castShadow; }
	inline bool Enable() const					    				{ return enable; }

private:
	Vec3 color = Vec3::Ones();
    float intencity = 2.0f;
	bool castShadow = true;	        	//此光源是否投射阴影
	bool enable = true;		        	//此光源是否启用

	// bias，分为固定偏移和斜率偏移两个
	float fogScattering = 0.005f;

	DirectionalLightInfo lightInfo;    

	Vec3 front = Vec3::UnitX();
	Vec3 up = Vec3::UnitY();

	void UpdateMatrix();


	void UpdateLightInfo();

private:
    BeginSerailize()
    SerailizeBaseClass(Component)
    SerailizeEntry(color)
	SerailizeEntry(castShadow)
	SerailizeEntry(enable)
    SerailizeEntry(intencity)
    SerailizeEntry(fogScattering)
    EndSerailize

	EnableComponentEditourUI()
	friend class RenderLightManager;
};