#pragma once

#include "Function/Render/RHI/RHIStructs.h"
#include "NRD/NRDIntegration.h"
#include "NRDSettings.h"
#include "RenderPass.h"
#include <cstdint>

class NRDPass : public RenderPass
{
public:
    NRDPass() {};
	~NRDPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "NRD"; }

	virtual PassType GetType() override final { return 	NRD_PASS; }

private:

    struct NRDSetting
    {
		uint32_t isBaseColorMetalnessAvailable = 1;
		uint32_t enableAntiFirefly = 1;
		uint32_t demodulate = 1;
		uint32_t denoisedOnly = 0;
		uint32_t specularOnly = 1;
		uint32_t restirEnabled = 0;
		float splitScreen = 0.0f;
		float motionVectorScaleX = 1.0f;
		float motionVectorScaleY = 1.0f;
		float motionVectorScaleZ = 1.0f;
    };
    NRDSetting setting = {};
	bool denoisedOnly = false;
	bool debug = false;

	nrd::CommonSettings commonSettings = {};

	nrd::RelaxSettings relaxSettings = {};
	nrd::ReblurSettings reblurSettings = {
		//.planeDistanceSensitivity = 0.35f
	};

    Shader computeShader[3];

    RHIRootSignatureRef rootSignature;
    RHIComputePipelineRef computePipeline[3];
	RHITextureRef confidenceTexture;

	NRDIntegration integration;
	uint32_t frameIndex = 0;
	EnablePassEditourUI()
};