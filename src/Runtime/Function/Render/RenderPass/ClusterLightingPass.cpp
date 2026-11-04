#include "ClusterLightingPass.h"
#include "Function/Global/Definations.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderPass/RenderPass.h"

void ClusterLightingPass::Init()
{
    auto backend = EngineContext::RHI();

    tileCullingShader = Shader(EngineContext::File()->ShaderPath() + "culling/zbin_tile_culling.comp.spv", SHADER_FREQUENCY_COMPUTE);
    binCullingShader  = Shader(EngineContext::File()->ShaderPath() + "culling/zbin_bin_culling.comp.spv",  SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo());
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;
    pipelineInfo.computeShader              = tileCullingShader.shader;
    tileCullingPipeline = backend->CreateComputePipeline(pipelineInfo);

    pipelineInfo.computeShader              = binCullingShader.shader;
    binCullingPipeline  = backend->CreateComputePipeline(pipelineInfo);
}

void ClusterLightingPass::Build(RDGBuilder& builder)
{
    if( IsEnabled() &&
        !EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) &&
        !EngineContext::Render()->IsPassEnabled(RAY_TRACING_BASE_PASS) &&
        !EngineContext::Render()->IsPassEnabled(PATH_TRACING_PASS))
    {
        RDGBufferHandle tileLights = builder.CreateBuffer("Tile Lights")
            .Import(EngineContext::RenderResource()->GetTileLightsBuffer(), RESOURCE_STATE_SHADER_RESOURCE)
            .Finish();

        // 子pass一：tile剔除（锥测试+前缀和，entry低24位清零落盘）
        builder.CreateComputePass(GetName() + " Tile Culling")
            .RootSignature(rootSignature)
            .Shaders({tileCullingShader.shader})
            .ReadWrite(2, SET2_BINDING_TILE_LIGHTS, 0, tileLights)
            .Execute([&](RDGPassContext context) {

                RHICommandListRef command = context.command;
                command->SetComputePipeline(tileCullingPipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->Dispatch(  Math::CeilDivide(uint32_t(LIGHT_CLUSTER_WIDTH), 4u),
                                    Math::CeilDivide(uint32_t(LIGHT_CLUSTER_HEIGHT), 2u),
                                    1);
            })
            .Finish();

        // 子pass二：bin位焊接
        builder.CreateComputePass(GetName() + " Bin Culling")
            .RootSignature(rootSignature)
            .Shaders({binCullingShader.shader})
            .ReadWrite(2, SET2_BINDING_TILE_LIGHTS, 0, tileLights)
            .Execute([&](RDGPassContext context) {

                RHICommandListRef command = context.command;
                command->SetComputePipeline(binCullingPipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->Dispatch(  uint32_t(LIGHT_CLUSTER_WIDTH),
                                    uint32_t(LIGHT_CLUSTER_HEIGHT),
                                    Math::CeilDivide(uint32_t(ZBIN_BIN_COUNT), 32u));
            })
            .Finish();
    }
}
