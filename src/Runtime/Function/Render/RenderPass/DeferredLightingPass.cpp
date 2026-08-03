#include "DeferredLightingPass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <memory>

void DeferredLightingPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader = Shader(EngineContext::File()->ShaderPath() + "default/deferred_lighting.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry({1, 0, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 1, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 2, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 3, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})
                     .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;
    pipelineInfo.computeShader              = computeShader.shader; 
    computePipeline   = backend->CreateComputePipeline(pipelineInfo);
}   

void DeferredLightingPass::Build(RDGBuilder& builder) 
{
    Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

    RDGTextureHandle diffuse        = builder.GetTexture("G-Buffer Diffuse/Metallic");
    RDGTextureHandle normal         = builder.GetTexture("G-Buffer Normal/Roughness");
    RDGTextureHandle emission       = builder.GetTexture("G-Buffer Emission");

    RDGTextureHandle outColor = builder.GetOrCreateTexture("Mesh Pass Out Color") 
        .Exetent({windowExtent.width, windowExtent.height, 1})
        .Format(EngineContext::Render()->GetHdrColorFormat())
        .ArrayLayers(1)
        .MipLevels(1)
        .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
        .AllowReadWrite()
        .AllowRenderTarget()
        .Finish();  

    if( IsEnabled() &&
        !EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) &&
        !EngineContext::Render()->IsPassEnabled(RAY_TRACING_BASE_PASS) && 
        !EngineContext::Render()->IsPassEnabled(PATH_TRACING_PASS))
    {
        RDGComputePassHandle pass = builder.CreateComputePass(GetName())
            .RootSignature(rootSignature)
            .ReadWrite(1, 0, 0, diffuse)
            .ReadWrite(1, 1, 0, normal)
            .ReadWrite(1, 2, 0, emission)
            .ReadWrite(1, 3, 0, outColor)
            .Execute([&](RDGPassContext context) {       

                DeferredLightingSetting newSetting = setting;
                if(EngineContext::Render()->IsPassEnabled(RESTIR_GI_PASS))
                    newSetting.directOnly = 1;

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(context.descriptors[1], 1);
                command->PushConstants(&newSetting, sizeof(DeferredLightingSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();
    }
}
