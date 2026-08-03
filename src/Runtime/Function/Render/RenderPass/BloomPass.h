#pragma once

#include "RenderPass.h"
#include <cstdint>

class BloomPass : public RenderPass
{
public:
    BloomPass() {};
	~BloomPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "Bloom"; }

	virtual PassType GetType() override final { return BLOOM_PASS; }

private:
	struct BloomSetting {
		int maxMip = 0;
		int mipLevel = 0;
		float stride = 1.0f;
		float bias = 6.0f;
		float accumulateIntencity = 2.0f;
		float combineIntencity = 0.01f; 
		float thresholdMin = 0.7f;
		float thresholdMax = 10.0f;
		uint32_t bloomOnly = 0;
	};
	BloomSetting setting;

    Shader computeShader[4];

    RHIComputePipelineRef computePipeline[4];

	RHIRootSignatureRef rootSignature;

	EnablePassEditourUI()
};