#pragma once

#include "RenderPass.h"
#include <cstdint>
#include <vector>

class TestTrianglePass : public RenderPass
{
public:
	TestTrianglePass() = default;
	~TestTrianglePass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "Test Triangle"; }

	virtual PassType GetType() override final { return TEST_TRIANGLE_PASS; }

private:
    Shader vertexShader;
	Shader fragmentShader;
	
    RHIRootSignatureRef rootSignature;
    RHIGraphicsPipelineRef graphicsPipeline;

private:
	EnablePassEditourUI()
};