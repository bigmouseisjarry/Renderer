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

	inline Frustum GetFrustum(int index)							{ return lightInfos[index].frustum; }

	virtual std::string GetTypeName() override						{ return "Directional Light Component"; }
	virtual ComponentType GetType() override						{ return DIRECTIONAL_LIGHT_COMPONENT; }

	bool ShouldUpdate(uint32_t cascade)								{ return updateCnts[cascade] == 0; }
	float GetConstantBias()											{ return constantBias; }
	float GetSlopeBias()											{ return slopeBias; }

	void SetColor(Vec3 color)										{ this->color = color; } 
	void SetIntencity(float intencity)								{ this->intencity = intencity; } 
	void SetCascadeSplit(float cascadeSplit)						{ this->cascadeSplitLambda = cascadeSplit; } 
	void SetUpdateFrequency(uint32_t cascade, int32_t frequency)	{ updateFrequences[cascade] = frequency; } 
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
	float cascadeSplitLambda = 0.95f;	//在对数划分和均匀划分间的加权权值
	std::array<int32_t, DIRECTIONAL_SHADOW_CASCADE_LEVEL> updateFrequences = { 0 };	//设置每级cascade更新频率
	std::array<int32_t, DIRECTIONAL_SHADOW_CASCADE_LEVEL> updateCnts = { 0 };
	float constantBias = 1.0f;          //bias，分为固定偏移和斜率偏移两个
	float slopeBias = 5.0f;
	float fogScattering = 0.005f;

	std::array<DirectionalLightInfo, DIRECTIONAL_SHADOW_CASCADE_LEVEL> lightInfos;          //向GPU提交的光源信息

	Vec3 front = Vec3::UnitX();
	Vec3 up = Vec3::UnitY();

	void UpdateMatrix();

	void UpdateCascades(); 

	void UpdateLightInfo();

private:
    BeginSerailize()
    SerailizeBaseClass(Component)
    SerailizeEntry(color)
	SerailizeEntry(castShadow)
	SerailizeEntry(enable)
    SerailizeEntry(intencity)
    SerailizeEntry(cascadeSplitLambda)
    SerailizeEntry(updateFrequences)
    SerailizeEntry(updateCnts)
    SerailizeEntry(constantBias)
    SerailizeEntry(slopeBias)
    SerailizeEntry(fogScattering)
    EndSerailize

	EnableComponentEditourUI()
	friend class RenderLightManager;
};