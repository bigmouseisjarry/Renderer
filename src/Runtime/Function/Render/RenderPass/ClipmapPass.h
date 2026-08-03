#pragma once

#include "Core/Math/Math.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/Buffer.h"
#include "Core/Clipmap/Clipmap.h"
#include "RenderPass.h"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class ClipmapPass : public RenderPass
{
public:
    ClipmapPass() {};
	~ClipmapPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "Clipmap"; }

	virtual PassType GetType() override final { return CLIPMAP_PASS; }

	std::shared_ptr<Clipmap> GetClipmap() { return clipmap; }

private:
	struct VXGISetting {
		ClipmapRegion region;
	};
	//VXGISetting setting = {};

    Shader rayGenShader;
	Shader rayMissShader;
	Shader closestHitShader;

    RHIRootSignatureRef rootSignature;
    RHIRayTracingPipelineRef rayTracingPipeline;

	RHITextureRef clipmapTexture;
	std::array<Buffer<ClipmapInfo>, FRAMES_IN_FLIGHT> clipmapInfoBuffer;

	bool attachToCamera = true;
	bool fullUpdate = false;
	Vec3 targetPos;
	std::shared_ptr<Clipmap> clipmap;
	std::vector<ClipmapRegion> updateRegions;

	EnablePassEditourUI()
};