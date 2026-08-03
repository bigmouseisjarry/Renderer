#pragma once

#include "RenderPass.h"
#include <cstdint>

class PostProcessingPass : public RenderPass
{
public:
    PostProcessingPass() {};
	~PostProcessingPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "Post Processing"; }

	virtual PassType GetType() override final { return POST_PROCESSING_PASS; }

private:
	struct PostProcessingSetting
	{
		float exposure = 0.4f;
		float luminance = 0.1f;
		float saturation = 1.0f;
		float contrast = 1.0f;
		uint32_t mode = 2;
		uint32_t fixLuminance = 0;
	};
	PostProcessingSetting setting;

    Shader computeShader;

    RHIRootSignatureRef rootSignature;
    RHIComputePipelineRef computePipeline;

	EnablePassEditourUI()
};