#pragma once

#include "Function/Global/Definations.h"
#include "Function/Render/RHI/RHIResource.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/Buffer.h"
#include "Function/Render/RenderSystem/RenderSurfaceCacheManager.h"
#include "RenderPass.h"
#include <cstdint>
#include <vector>

class SurfaceCachePass : public RenderPass
{
public:
	SurfaceCachePass() = default;
	~SurfaceCachePass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "Surface Cache"; }

	virtual PassType GetType() override final { return SURFACE_CACHE_PASS; }

private:
	struct SurfaceCacheRasterizeSetting {
		uint32_t meshCardID;
		uint32_t objectID;
		uint32_t vertexID;
	};

    Shader vertexShader;
	Shader fragmentShader;
	Shader computeShader;

    RHIRootSignatureRef rootSignature;
    RHIGraphicsPipelineRef graphicsPipeline;
	RHIComputePipelineRef computePipeline;

	ArrayBuffer<SurfaceCacheLightingDispatch, 
		(MAX_SURFACE_CACHE_DIRECT_LIGHTING_SIZE / SURFACE_CACHE_DIRECT_LIGHTING_TILE_SIZE) * 
		(MAX_SURFACE_CACHE_DIRECT_LIGHTING_SIZE / SURFACE_CACHE_DIRECT_LIGHTING_TILE_SIZE)> directLightingBuffer[FRAMES_IN_FLIGHT];

	std::vector<ClearAttachment> clearAttachments;

	bool init = false;

private:
	EnablePassEditourUI()
};