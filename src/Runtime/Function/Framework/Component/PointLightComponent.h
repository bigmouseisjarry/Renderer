#pragma once

#include "Component.h"
#include "Core/Math/BoundingBox.h"
#include "Core/Math/Math.h"
#include "Core/Serialize/Serializable.h"
#include "Function/Global/Definations.h"
#include "Function/Render/RenderResource/RenderStructs.h"
#include <cstdint>

class PointLightComponent : public Component
{
public:
	PointLightComponent() = default;
	~PointLightComponent();

	virtual void OnInit() override;
	virtual void OnUpdate(float deltaTime) override;

    void SetScale(float scale)                      { this->far = scale; }
    void SetColor(Vec3 color)                       { this->color = color; }
    void SetIntencity(float intencity)              { this->intencity = intencity; }
    void SetCastShadow(bool castShadow)             { this->castShadow = castShadow; }
    void SetFogScattering(float fogScattering)      { this->fogScattering = fogScattering; }
    void SetEnable(bool enable)                     { this->enable = enable; }

    virtual std::string GetTypeName() override		{ return "Point Light Component"; }
	virtual ComponentType GetType() override	    { return POINT_LIGHT_COMPONENT; }

	inline BoundingSphere GetBoundingSphere() const	{ return sphere; }

	inline bool CastShadow() const				    { return castShadow; }
	inline bool Enable() const					    { return enable; }

    inline uint32_t GetPointLightID()               { return pointLightID; }

private:
    uint32_t pointLightID = 0;

private:
    float near = 0.1f;          //光源设置
    float far = 25.0f;
    Vec3 color = Vec3::Ones();
    float intencity = 2.0f;
    float fogScattering = 0.02f;
	bool castShadow = true;	        //此光源是否投射阴影
	bool enable = true;		        //此光源是否启用
           
	BoundingSphere sphere;          //包围球

    PointLightInfo info;            //向GPU提交的光源信息

    void UpdateLightInfo(uint32_t gpuSlot);    // 上传槽位=帧内密集号（1..cnt），由RenderLightManager分配

private:
    BeginSerailize()
    SerailizeBaseClass(Component)
    SerailizeEntry(far)
    SerailizeEntry(color)
    SerailizeEntry(intencity)
    SerailizeEntry(fogScattering)
    SerailizeEntry(castShadow)
    SerailizeEntry(enable)
    EndSerailize

    EnableComponentEditourUI()
    friend class RenderLightManager;
};