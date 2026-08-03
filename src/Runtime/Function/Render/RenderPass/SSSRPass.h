#pragma once

#include "RenderPass.h"

class SSSRPass : public RenderPass
{
public:
    SSSRPass() {};
	~SSSRPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "SSSR"; }

	virtual PassType GetType() override final { return SSSR_PASS; }

private:
	struct SSSRSetting 
	{
		int mode = 1;
		int maxMip = 7;			// 一个限制条件是hiz分辨率必须是二次幂，否则每层mip边缘对不上
		int startMip = 0;		// 这里没有单独处理，因此只能用前1152 / 9 = 128 = 2^7 层?  实际用10好像也行
		int endMip = 0;
		int maxLoop = 200;

		float maxRoughness = 0.75f;
		float minHitDistance = 0.01f;
		float importanceSampleBias = 0.1f;
		float thickness = 0.01;
		float screenFade = 0.2f;
		float filterBlend = 0.95f;	

		int enableSkybox = 1;		
		int reflectionOnly = 0;
		int visualizeHitUV = 0;
		int visualizeHitMask = 0;
	};
	SSSRSetting setting = {};

    Shader computeShader[4];

    RHIRootSignatureRef rootSignature;
    RHIComputePipelineRef computePipeline[4];

	TextureRef historyColorTex;

	EnablePassEditourUI()
};