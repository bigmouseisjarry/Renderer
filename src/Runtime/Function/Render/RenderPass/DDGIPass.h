#pragma once

#include "Function/Framework/Component/VolumeLightComponent.h"
#include "Function/Global/Definations.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "RenderPass.h"
#include <array>
#include <cstdint>
#include <vector>

class DDGIPass : public RenderPass
{
public:
    DDGIPass() {};
	~DDGIPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "DDGI"; }

	virtual PassType GetType() override final { return DDGI_PASS; }

private:
	struct DDGIComputeSetting
	{
		uint32_t volumeLightID = 0;
		float _padding[3];
	};
	//DDGIComputeSetting setting = {};

    Shader rayGenShader;
	Shader rayMissShader;
	Shader closestHitShader;
	Shader computeShader[5];

    RHIRootSignatureRef rootSignature;
    RHIRayTracingPipelineRef rayTracingPipeline;
	RHIComputePipelineRef computePipeline[5];

	std::array<std::vector<std::shared_ptr<VolumeLightComponent>>, FRAMES_IN_FLIGHT> volumeLights;

	EnablePassEditourUI()
};