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
		uint32_t side = 0;          // view_z用：0=双边(legacy), 1=仅specular(q0侧), 2=仅diffuse(q1侧)——NRD双实例拆分
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


	RHIRootSignatureRef copySssrRootSignature;
	RHIRootSignatureRef viewZRootSignature;
	RHIRootSignatureRef combineRootSignature;
    // RHIRootSignatureRef rootSignature;
    RHIComputePipelineRef computePipeline[3];
	RHITextureRef confidenceTexture;

	// 第3刀：NRD拆双实例——spec(跟SSSR链走q0空窗,ForceGraphics)与diffuse(跟ReSTIR留q1)。
	// 各自独立nrd::Instance/常量缓冲/纹理池，RDG名字带前缀不撞黑板
	NRDIntegration integrationSpec;
	NRDIntegration integrationDiff;
	uint32_t frameIndex = 0;
	EnablePassEditourUI()
};