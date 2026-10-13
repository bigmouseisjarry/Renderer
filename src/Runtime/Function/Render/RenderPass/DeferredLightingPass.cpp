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
                     .AddEntry({1, 0, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })
                     .AddEntry({1, 1, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })
                     .AddEntry({1, 2, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })
                     .AddEntry({1, 3, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })
                     .AddEntry({1, 4, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 5, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })
                     .AddEntry({1, 6, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })
                     .AddEntry({1, 7, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })
                     .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;
    pipelineInfo.computeShader              = computeShader.shader; 
    computePipeline   = backend->CreateComputePipeline(pipelineInfo);

    Extent2D extent = EngineContext::Render()->GetWindowsExtent();

    directionalDepthHistoryTex = std::make_shared<Texture>(
        TEXTURE_TYPE_2D,
        FORMAT_D32_SFLOAT,
        Extent3D(DIRECTIONAL_SHADOW_SIZE, DIRECTIONAL_SHADOW_SIZE, 1),
        1, 1);
}   

void DeferredLightingPass::Build(RDGBuilder& builder) 
{
    Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

    RDGTextureHandle diffuse        = builder.GetTexture("G-Buffer Diffuse/Metallic");
    RDGTextureHandle normal         = builder.GetTexture("G-Buffer Normal/Roughness");
    RDGTextureHandle emission       = builder.GetTexture("G-Buffer Emission");
    RDGTextureHandle geometricNormal = builder.GetTexture("G-Buffer Geometric Normal/AO");
    RDGTextureHandle materialParams = builder.GetTexture("G-Buffer Specular F0/Roughness");
    RDGTextureHandle ior             = builder.GetTexture("G-Buffer IOR");

    RDGTextureHandle directionalDepthHistory = builder.CreateTexture("Directional Depth History")
        .Import(directionalDepthHistoryTex->texture, RESOURCE_STATE_UNDEFINED)
        .Finish();

    RDGTextureHandle outColor = builder.GetOrCreateTexture("Noisy HDR Color") 
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
            .Read(1, 0, 0, diffuse)
            .Read(1, 1, 0, normal)
            .Read(1, 2, 0, emission)
            .Read(1, 3, 0, directionalDepthHistory)
            .Read(1, 5, 0, geometricNormal)
            .Read(1, 6, 0, materialParams)
            .Read(1, 7, 0, ior)
            .ReadWrite(1, 4, 0, outColor)
            .Read(builder.GetTexture("Directional Depth [0]"))
            .Read(builder.GetTexture("Directional Depth [1]"))
            .Read(builder.GetTexture("Directional Depth [2]"))
            .Read(builder.GetTexture("Directional Depth [3]"))
            .Read(builder.GetTexture("Point Shadow Filtered Color [0]"))
            .Read(builder.GetTexture("Point Shadow Filtered Color [1]"))
            .Read(builder.GetTexture("Light Cluster Grid"))
            .Read(builder.GetBuffer("Light Cluster Index"))
            .Execute([&](RDGPassContext context) {

                DeferredLightingSetting newSetting = setting;
                if (EngineContext::Render()->IsPassEnabled(RESTIR_GI_PASS))
                    newSetting.directOnly = 1;

                RHICommandListRef command = context.command;
                command->SetComputePipeline(computePipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(context.descriptors[1], 1);
                command->PushConstants(&newSetting, sizeof(DeferredLightingSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16),
                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16),
                    1);
            })
            .Finish();
    }

    RDGCopyPassHandle copyPass = builder.CreateCopyPass(GetName() + "Copy Directional Depth [0]")
        .From(builder.GetTexture("Directional Depth [0]"))
        .To(directionalDepthHistory)
        .Finish();

}
