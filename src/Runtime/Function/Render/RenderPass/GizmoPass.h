#pragma once

#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/Model.h"
#include "RenderPass.h"
#include <memory>

class GizmoPass : public RenderPass
{
public:
	GizmoPass() = default;
	~GizmoPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "Gizmo"; }

	virtual PassType GetType() override final { return GIZMO_PASS; }

private:
	Shader computeShader;
    Shader vertexShader[3];
	Shader geometryShader[2];
	Shader fragmentShader[2];

    RHIRootSignatureRef rootSignature;
	RHIComputePipelineRef computePipeline;
    RHIGraphicsPipelineRef graphicsPipeline[4];

	std::shared_ptr<Model> cubeWire;
	std::shared_ptr<Model> sphereWire;
	RHIIndexedIndirectCommand command[4];

private:
	EnablePassEditourUI()
};