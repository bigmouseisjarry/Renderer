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
	};
	DeferredLightingSetting setting = {};

    Shader localShader;           // 阶段①：局部灯块（覆盖写=emission+局部灯直接光）
    Shader directionalShader;     // 阶段②：方向灯块非太阳（养着，恒不声明；多方向光刀启用）
    Shader sunShader;             // 阶段③：太阳（atmo插拔点在此）
    Shader compositeShader;       // 阶段④：DDGI间接+debug动物园+终写

    RHIRootSignatureRef rootSignature;

    RHIComputePipelineRef localPipeline;
    RHIComputePipelineRef directionalPipeline;
    RHIComputePipelineRef sunPipeline;
    RHIComputePipelineRef compositePipeline;

private:
    EnablePassEditourUI()
};
