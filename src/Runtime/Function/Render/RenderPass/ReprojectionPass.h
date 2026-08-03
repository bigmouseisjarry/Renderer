#pragma once

#include "Function/Render/RHI/RHIStructs.h"
#include "RenderPass.h"

class ReprojectionPass : public RenderPass
{
public:
    ReprojectionPass() {};
	~ReprojectionPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "Reprojection"; }

	virtual PassType GetType() override final { return REPROJECTION_PASS; }

private:
    Shader computeShader;

    RHIRootSignatureRef rootSignature;
    RHIComputePipelineRef computePipeline;

	EnablePassEditourUI()
};