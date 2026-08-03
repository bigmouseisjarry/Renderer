#pragma once

#include "Component.h"
#include "Core/Math/BoundingBox.h"
#include "Core/Math/Math.h"
#include "Core/Serialize/Serializable.h"
#include "Function/Global/Definations.h"
#include "Function/Render/RenderResource/RenderResourceManager.h"
#include "Function/Render/RenderResource/RenderStructs.h"
#include <cstdint>

class VolumeLightComponent : public Component
{
public:
	VolumeLightComponent() = default;
	~VolumeLightComponent();

	virtual void OnInit() override;
	virtual void OnUpdate(float deltaTime) override;

    void SetEnable(bool enable)                     { this->enable = enable; }
    void SetBlendWeight(float blendWeight)          { this->blendWeight = blendWeight; }
    void SetProbeCounts(IVec3 probeCounts)          { this->probeCounts = probeCounts;  UpdateTexture(); }
    void SetGridStep(Vec3 gridStep)                 { this->gridStep = gridStep; }
    void SetRaysPerProbe(int raysPerProbe)          { this->raysPerProbe = raysPerProbe; UpdateTexture(); }
    void SetNormalBias(float normalBias)            { this->normalBias = normalBias; }
    void SetEnergyPreservation(float energy)        { this->energyPreservation = energy; }
    void SetDepthSharpness(float depthSharpness)    { this->depthSharpness = depthSharpness; }
    void SetVisibilityTest(bool visibilityTest)     { this->visibilityTest = visibilityTest; }
    void SetInfiniteBounce(bool infiniteBounce)     { this->infiniteBounce = infiniteBounce; }
    void SetUpdateFrequence(int updateFrequence)    { this->updateFrequences[0] = updateFrequence; 
                                                      this->updateFrequences[1] = updateFrequence; }
    void SetVisulaize(bool visulaize)               { this->visulaize = visulaize; }
    void SetProbeScale(float scale)                 { this->visulaizeProbeScale = scale; }
                                                      
    inline IVec3 GetProbeCounts()                   { return this->probeCounts;}
    inline Vec3 GetGridStep()                       { return this->gridStep; }
    inline int GetRaysPerProbe()                    { return this->raysPerProbe; }
    inline int GetVisulaizeMode()                   { return this->visulaizeMode; }
    inline float GetVisulaizeProbeScale()           { return this->visulaizeProbeScale; }

	inline bool Enable() const					    { return enable; }
    inline bool Visualize() const					{ return visulaize; }
    inline bool ShouldUpdate(int index)             { return shouldUpdate[index]; } 
    inline const VolumeLightTextures& GetTextres()  { if(!textures.diffuseTex) UpdateTexture(); 
                                                      return textures; } 

    inline BoundingBox GetBoundingBox() const	    { return box; }

    inline uint32_t GetVolumeLightID()              { return volumeLightID; }                                                          

    virtual std::string GetTypeName() override		{ return "Volume Light Component"; }
	virtual ComponentType GetType() override	    { return VOLUME_LIGHT_COMPONENT; }

private:
    uint32_t volumeLightID = 0;

    VolumeLightTextures textures;

private:
    bool enable = true;                             
    IVec3 probeCounts = IVec3(10, 10, 10);      // 探针数量
    Vec3 gridStep = Vec3(3.0f, 3.0f, 3.0f);     // 探针间隔
    int raysPerProbe = 256;                             // 探针射线数量
    float normalBias = 0.25f;
    float energyPreservation = 1.0f;                    // 能量守恒系数，防止光爆掉
    float depthSharpness = 50.0f;
    float blendWeight = 0.95f;
    bool visibilityTest = true;
    bool infiniteBounce = true;
    bool randomOrientation = true;

    bool visulaize = true;                              // 启用探针可视化
    int visulaizeMode = 0;                              // 可视化模式
    float visulaizeProbeScale = 0.3f;                   // 可视化探针尺寸

	int updateFrequences[2]	= { 1, 1 };			//分成两部分更新：更新场景几何信息和更新光照
	int updateCnts[2]		= { 0, -1 };
	bool shouldUpdate[2]    = { false , false };			

	BoundingBox box;                 //包围盒

    VolumeLightInfo info;            //向GPU提交的光源信息

    void UpdateLightInfo();

    void UpdateTexture();

private:
    BeginSerailize()
    SerailizeBaseClass(Component)
    SerailizeEntry(enable)
    SerailizeEntry(probeCounts)
    SerailizeEntry(gridStep)
    SerailizeEntry(raysPerProbe)
    SerailizeEntry(normalBias)
    SerailizeEntry(energyPreservation)
    SerailizeEntry(depthSharpness)
    SerailizeEntry(blendWeight)
    SerailizeEntry(enable)
    SerailizeEntry(visibilityTest)
    SerailizeEntry(infiniteBounce)
    SerailizeEntry(randomOrientation)
    SerailizeEntry(visulaize)
    SerailizeEntry(visulaizeMode)
    SerailizeEntry(visulaizeProbeScale)
    SerailizeEntry(updateFrequences)
    EndSerailize

    EnableComponentEditourUI()
    friend class RenderLightManager;
};