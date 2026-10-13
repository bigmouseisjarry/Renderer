#pragma once

#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/Shader.h"
#include "MeshPass.h"
#include "RenderPass.h"
#include <cstdint>

// 薄透明二次绘制（HRP式queue2500语义）：THIN掩码材质在不透明G-Buffer之后叠写同一组纹理。
// depthWrite=false保持HiZ/SSR纯不透明语义；复用同一个deferred.frag零变体（绑全8附件同序，全LOAD）。
// 材质规约：thin材质只勾THIN掩码（DEFERRED/FORWARD+THIN会被对应pass再画一遍）；
// useForDepthPass建议关闭，避免深度预pass写入薄层深度后thin的LESS_EQUAL与自己z-fight。
class GBufferThinPass : public MeshPass
{
public:
    GBufferThinPass() {};
	~GBufferThinPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "G-Buffer Thin"; }

	virtual PassType GetType() override final { return G_BUFFER_THIN_PASS; }

private:
    Shader vertexShader;
    Shader clusterVertexShader;
    Shader fragmentShader;

    RHIRootSignatureRef rootSignature;
    RHIGraphicsPipelineRef pipeline;
    RHIGraphicsPipelineRef clusterPipeline;

    friend class GBufferThinPassProcessor;

private:
    EnablePassEditourUI()
};

class GBufferThinPassProcessor : public MeshPassProcessor
{
public:
    GBufferThinPassProcessor(GBufferThinPass* pass) { this->pass = pass; }

    virtual void OnCollectBatch(const DrawBatch& batch) override final;
    virtual RHIGraphicsPipelineRef OnCreatePipeline(const DrawPipelineState& pipelineState) override final;

private:
    GBufferThinPass* pass;
};
