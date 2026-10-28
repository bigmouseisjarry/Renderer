#include "FXAAPass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <cstdint>

void FXAAPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader = Shader(EngineContext::File()->ShaderPath() + "post_process/fxaa.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())   
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo())       
                     .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;
    pipelineInfo.computeShader              = computeShader.shader;
    computePipeline   = backend->CreateComputePipeline(pipelineInfo);
}   

void FXAAPass::Build(RDGBuilder& builder) 
{
    enable = IsEnabled() ? 1.0f : 0.0f;

    Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

    RDGTextureHandle inColor = builder.GetTexture("Bloom Out Color");
    
    RDGTextureHandle outColor = builder.GetOrCreateTexture("FXAA Out Color")
        .Exetent({windowExtent.width, windowExtent.height, 1})
        .Format(EngineContext::Render()->GetHdrColorFormat())
        .ArrayLayers(1)
        .MipLevels(1)
        .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
        .AllowReadWrite()
        .AllowRenderTarget()
        .Finish();  

    RDGComputePassHandle pass = builder.CreateComputePass(GetName())
        .AddFlag(RDGPassFlags::ForceGraphicsQueue)
        .RootSignature(rootSignature)
        .Shaders({computeShader.shader})             
        .Read(2, SET2_BINDING_IN_COLOR, 0, inColor)
        .ReadWrite(2, SET2_BINDING_OUT_COLOR, 0, outColor)
        .Execute([&](RDGPassContext context) {       

            RHICommandListRef command = context.command; 
            command->SetComputePipeline(computePipeline);
            command->BindDescriptorSet(context.descriptors[2], 2);
            command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
            command->PushConstants(&enable, sizeof(uint32_t), SHADER_FREQUENCY_COMPUTE);
            command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                1);
        })
        .Finish();
}
