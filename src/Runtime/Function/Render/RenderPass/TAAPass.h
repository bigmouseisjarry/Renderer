#pragma once

#include "Function/Render/RHI/RHIStructs.h"
#include "RenderPass.h"

class TAAPass : public RenderPass
{
public:
    TAAPass() {};
	~TAAPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "TAA"; }

	virtual PassType GetType() override final { return TAA_PASS; }

private:
    struct TAASetting 
    {
        int enable = 1;
        int sharpen = 0;
        int reprojectionOnly = 0;
        int showVelocity = 0;
        int showReprojectionVaild = 0;
		float velocityFactor = 1.0f;
		float blendFactor = 0.1f;
        
    };
    TAASetting setting = {};

    Shader computeShader;

    RHIRootSignatureRef rootSignature;
    RHIComputePipelineRef computePipeline;
    RHITextureRef historyTex;

	EnablePassEditourUI()
};