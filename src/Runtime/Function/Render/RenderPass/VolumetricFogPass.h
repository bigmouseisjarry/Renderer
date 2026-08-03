#pragma once

#include "RenderPass.h"
#include <cstdint>

class VolumetricFogPass : public RenderPass
{
public:
	VolumetricFogPass() = default;
	~VolumetricFogPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "Volumetric Fog"; }

	virtual PassType GetType() override final { return VOLUMETIRC_FOG_PASS; }

private:
	struct VolumetricFogSetting {
		float henyeyGreensteinG			= 0.0f;
		float attenuationFactor			= 0.01f;	// 衰减系数 = 外散射系数 + 吸收率
		float scatteringIntencity		= 1.0f;
		float blendFactor				= 0.1f;
		float indirectLightIntencity 	= 0.0f;
		uint32_t isotropic 				= 1;
		uint32_t fogOnly 				= 0;
	};
	VolumetricFogSetting setting = {};

    Shader computeShader[3];

    RHIRootSignatureRef rootSignature;
    RHIComputePipelineRef computePipeline[3];

	TextureRef historyColorTex;

	uint32_t groupCountX = 10;
	uint32_t groupCountY = 10;
	uint32_t groupCountZ = VOLUMETRIC_FOG_SIZE_Z;

private:
	EnablePassEditourUI()
};