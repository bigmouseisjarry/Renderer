#pragma once

#include "Function/Framework/Component/VolumeLightComponent.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/Model.h"
#include "RenderPass.h"

class DDGIVisualizePass : public RenderPass
{
public:
	DDGIVisualizePass() = default;
	~DDGIVisualizePass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "DDGI Visualize"; }

	virtual PassType GetType() override final { return DDGI_VISUALIZE_PASS; }

private:
	struct DDGIVisualizeSetting
	{
		int volumeLightID = 0;
		float probeScale = 0.1f;
		int visualizeMode = 0;
		int vertexID = 0;
	};
	//DDGIVisualizeSetting setting = {};

    Shader vertexShader;
	Shader fragmentShader;

    RHIRootSignatureRef rootSignature;
    RHIGraphicsPipelineRef graphicsPipeline;

	ModelRef model;

	std::array<std::vector<std::shared_ptr<VolumeLightComponent>>, FRAMES_IN_FLIGHT> volumeLights;
	
private:
	EnablePassEditourUI()
};