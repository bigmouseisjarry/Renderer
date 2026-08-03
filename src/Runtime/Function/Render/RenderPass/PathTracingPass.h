#pragma once

#include "Function/Render/RHI/RHIStructs.h"
#include "RenderPass.h"
#include <cstdint>

class PathTracingPass : public RenderPass
{
public:
    PathTracingPass() {};
	~PathTracingPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "Path Tracing"; }

	virtual PassType GetType() override final { return PATH_TRACING_PASS; }

private:
	struct PathTracingSetting{
		int numSamples = 1;			// 每帧采样数
		int totalNumSamples = 0;	// 累计采样数
		int numBounce = 5;			// 光线反射深度
		
		int sampleSkyBox = 1;		// 是否采样来自天空盒的光照
		int indirectOnly = 0;		// 仅间接光照
		int diffuseOnly = 0;		// 仅漫反射
		int mode = 0;
	};
	PathTracingSetting setting = {};

    Shader rayGenShader;
	Shader rayMissShader;
	Shader closestHitShader;

    RHIRootSignatureRef rootSignature;
    RHIRayTracingPipelineRef rayTracingPipeline;

	RHITextureRef historyColorTex;

	EnablePassEditourUI()
};