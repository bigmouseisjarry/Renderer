#pragma once

#include "Core/Math/Math.h"
#include "Core/Clipmap/Clipmap.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "RenderPass.h"

class ClipmapVisualizePass : public RenderPass
{
public:
	ClipmapVisualizePass() = default;
	~ClipmapVisualizePass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "Clipmap Visualize"; }

	virtual PassType GetType() override final { return CLIPMAP_VISUALIZE_PASS; }

private:
	struct ClipmapVisualizeSetting {
		int mipLevel = 0;
		int minMip = 0; 
		int maxMip = 10;
	};
	ClipmapVisualizeSetting setting = {};
	int mode = 0;

    Shader vertexShader;
	Shader geometryShader;
	Shader fragmentShader;

	Shader rayGenShader;
	Shader rayMissShader;
	Shader closestHitShader;

    RHIRootSignatureRef rootSignature;
	RHIRayTracingPipelineRef rayTracingPipeline;
    RHIGraphicsPipelineRef graphicsPipeline;

	std::shared_ptr<Clipmap> clipmap;

private:
	EnablePassEditourUI()
};