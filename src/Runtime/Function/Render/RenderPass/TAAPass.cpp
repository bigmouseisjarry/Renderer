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
                     .AddEntry({1, 0, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})
                     .AddEntry({1, 1, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})
                     .AddEntry({1, 2, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})
                     .AddEntry({1, 3, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 4, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})   // HISTORY_OUT：kernel直写下帧history（原Copy History pass已删）
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

    // 第1刀：TAA history融合进kernel。taa.comp对HISTORY_COLOR做重投影双线性采样（跨像素读），
    // 同dispatch直写同纹理会污染历史——必须ping-pong：读本帧parity侧，写另一侧。
    // parity=CurrentFrameIndex()（FRAMES_IN_FLIGHT=2严格交替）；SSSR pyramid（第2刀）GetOrCreate同名读侧，
    // SSSR先Build时由此处创建、后Build时Import断言幂等（同纹理同state）。
    uint32_t historyIndex = EngineContext::CurrentFrameIndex();
    RDGTextureHandle historyColor = builder.GetOrCreateTexture("Final Color History")
        .Import(EngineContext::RenderResource()->GetFinalColorHistoryTexture(historyIndex), RESOURCE_STATE_UNDEFINED)
        .Finish();

    RDGTextureHandle historyColorNext = builder.CreateTexture("Final Color History Next")
        .Import(EngineContext::RenderResource()->GetFinalColorHistoryTexture(historyIndex ^ 1), RESOURCE_STATE_UNDEFINED)
        .Finish();

    RDGTextureHandle reprojectionOut = builder.GetTexture("Reprojection Out");

    RDGTextureHandle outColor = builder.CreateTexture("TAA Out Color")
        .Exetent({windowExtent.width, windowExtent.height, 1})
        .Format(EngineContext::Render()->GetHdrColorFormat())
        .ArrayLayers(1)
        .MipLevels(1)
        .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
        .AllowReadWrite()
        .AllowRenderTarget()
        .Finish();  

    // 第3刀·尾部加强：post链钉graphics队列（与Forward/Bloom同流，帧尾零跨队列跳）
    RDGComputePassHandle pass = builder.CreateComputePass(GetName())
        .AddFlag(RDGPassFlags::ForceGraphicsQueue)
        .RootSignature(rootSignature)
        .Read(1, 0, 0, inColor)
        .Read(1, 1, 0, historyColor)
        .Read(1, 2, 0, reprojectionOut)
        .ReadWrite(1, 3, 0, outColor)
        .ReadWrite(1, 4, 0, historyColorNext)
        .Execute([&](RDGPassContext context) {

            RHICommandListRef command = context.command;
            command->SetComputePipeline(computePipeline);
            command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
            command->BindDescriptorSet(context.descriptors[1], 1);
            command->PushConstants(&setting, sizeof(TAASetting), SHADER_FREQUENCY_COMPUTE);
            command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16),
                                Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16),
                                1);
        })
        .Finish();
}
