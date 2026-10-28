#include "TAAPass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <cstdint>

void TAAPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader = Shader(EngineContext::File()->ShaderPath() + "post_process/taa.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo())       // 刀7b-3：set2全局统一RS（98槽；b36已死——taa.comp的imageStore(HISTORY_OUT)已删）
                     .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;
    pipelineInfo.computeShader              = computeShader.shader;
    computePipeline   = backend->CreateComputePipeline(pipelineInfo);
}

void TAAPass::Build(RDGBuilder& builder) 
{
    setting.enable = IsEnabled() ? 1.0f : 0.0f;

    Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

    RDGTextureHandle inColor = builder.GetTexture("FXAA Out Color");

    // 乒乓两槽稳态均=SRV：写槽经Exposure/Post读收敛、读槽由本pass读收敛（parity翻转不改变终点）
    RDGTextureHandle historyColor = builder.GetOrCreateTexture("Stable HDR Color History")
        .Import(EngineContext::RenderResource()->GetStableHDRColorTexture(1))
        .Finish();

    RDGTextureHandle reprojectionOut = builder.GetTexture("Reprojection Out");

    RDGTextureHandle outColor = builder.CreateTexture("Stable HDR Color")
        .Import(EngineContext::RenderResource()->GetStableHDRColorTexture(0))  // TAA写UAV后由Exposure/Post读收敛SRV
        .Finish();

    RDGComputePassHandle pass = builder.CreateComputePass(GetName())
        .AddFlag(RDGPassFlags::ForceGraphicsQueue)
        .RootSignature(rootSignature)
        .Shaders({computeShader.shader})             // 刀7b-3：严格边校验
        .ReadIfValid(2, SET2_BINDING_HISTORY_VELOCITY, 0, "G-Buffer Velocity")
        .Read(2, SET2_BINDING_TAA_CURRENT_COLOR, 0, inColor)
        .Read(2, SET2_BINDING_TAA_HISTORY_COLOR, 0, historyColor)
        .Read(2, SET2_BINDING_TAA_REPROJECTION_RESULT, 0, reprojectionOut)
        .ReadWrite(2, SET2_BINDING_TAA_OUT_COLOR, 0, outColor)
        .Execute([&](RDGPassContext context) {

            RHICommandListRef command = context.command;
            command->SetComputePipeline(computePipeline);
            command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
            command->BindDescriptorSet(context.descriptors[2], 2);
            command->PushConstants(&setting, sizeof(TAASetting), SHADER_FREQUENCY_COMPUTE);
            command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16),
                                Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16),
                                1);
        })
        .Finish();
}
