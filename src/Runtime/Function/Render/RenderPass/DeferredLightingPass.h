#pragma once

#include "RenderPass.h"
#include <cstdint>

class DeferredLightingPass : public RenderPass
{
public:
	DeferredLightingPass() = default;
	~DeferredLightingPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "Deferred Lighting"; }

	virtual PassType GetType() override final { return DEFERRED_LIGHTING_PASS; }

private:
	struct DeferredLightingSetting{
		uint32_t mode = 0;
		uint32_t directOnly = 1;	// 在使用ReSTIR GI时，DDGI仅用于surface cache的光照，不用于G-Buffer光照
		uint32_t enbalePSCC = 1;
		uint32_t PCFMode = 3;
	};
	DeferredLightingSetting setting = {};

	TextureRef directionalDepthHistoryTex;

    Shader computeShader;

    RHIRootSignatureRef rootSignature;
    RHIComputePipelineRef computePipeline;

private:
	EnablePassEditourUI()
};