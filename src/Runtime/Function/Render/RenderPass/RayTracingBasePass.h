#pragma once

#include "Function/Render/RHI/RHIStructs.h"
#include "RenderPass.h"

class RayTracingBasePass : public RenderPass
{
public:
    RayTracingBasePass() {};
	~RayTracingBasePass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "Ray Trace Base"; }

	virtual PassType GetType() override final { return RAY_TRACING_BASE_PASS; }

private:
	struct RayTracingBaseSetting{
		uint32_t mode = 0;
	};
	RayTracingBaseSetting setting = {};

    Shader rayGenShader;
	Shader rayMissShader;
	Shader closestHitShader;

    RHIRootSignatureRef rootSignature;
    RHIRayTracingPipelineRef rayTracingPipeline;

	EnablePassEditourUI()
};