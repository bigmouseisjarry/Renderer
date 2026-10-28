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
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo())       
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
    RDGTextureHandle geometricNormal = builder.GetTexture("G-Buffer Geometric Normal/AO");
    RDGTextureHandle materialParams = builder.GetTexture("G-Buffer Specular F0/Roughness");
    RDGTextureHandle ior             = builder.GetTexture("G-Buffer IOR");

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
        auto pass = builder.CreateComputePass(GetName())   
            .RootSignature(rootSignature)
            .Shaders({computeShader.shader})             
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))
            .ReadWrite(2, SET2_BINDING_LIGHT_CLUSTER_GRID, 0, builder.GetTexture("Light Cluster Grid"), VIEW_TYPE_3D)
            .ReadWrite(2, SET2_BINDING_LIGHT_CLUSTER_INDEX, 0, builder.GetBuffer("Light Cluster Index"), 0, 0)
            .ReadIfValid(2, SET2_BINDING_DIRECTIONAL_SHADOW, 0, "Directional Depth [0]", VIEW_TYPE_2D, { TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1 })
            .ReadIfValid(2, SET2_BINDING_DIRECTIONAL_SHADOW, 1, "Directional Depth [1]", VIEW_TYPE_2D, { TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1 })
            .ReadIfValid(2, SET2_BINDING_DIRECTIONAL_SHADOW, 2, "Directional Depth [2]", VIEW_TYPE_2D, { TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1 })
            .ReadIfValid(2, SET2_BINDING_DIRECTIONAL_SHADOW, 3, "Directional Depth [3]", VIEW_TYPE_2D, { TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1 })
            .ReadIfValid(2, SET2_BINDING_POINT_SHADOW, 0, "Point Shadow Filtered Color [0]", VIEW_TYPE_CUBE, { TEXTURE_ASPECT_COLOR, 0, 1, 0, 6 })
            .ReadIfValid(2, SET2_BINDING_POINT_SHADOW, 1, "Point Shadow Filtered Color [1]", VIEW_TYPE_CUBE, { TEXTURE_ASPECT_COLOR, 0, 1, 0, 6 });
            for(auto& volumeLight : EngineContext::Render()->GetLightManager()->GetVolumeLights())
            {
                uint32_t vid = volumeLight->GetVolumeLightID();
                RDGTextureHandle irr = builder.GetOrCreateTexture("Volume Light Irradiance [" + std::to_string(vid) + "]")
                    .Import(volumeLight->GetTextres().irradianceTex->texture).Finish();     
                RDGTextureHandle dep = builder.GetOrCreateTexture("Volume Light Depth [" + std::to_string(vid) + "]")
                    .Import(volumeLight->GetTextres().depthTex->texture).Finish();
                pass.Read(2, SET2_BINDING_VOLUME_LIGHT_IRRADIANCE, vid, irr, VIEW_TYPE_2D, {});
                pass.Read(2, SET2_BINDING_VOLUME_LIGHT_DEPTH, vid, dep, VIEW_TYPE_2D, {});
            }
            pass
            .Read(2, SET2_BINDING_GBUFFER_DIFFUSE_METALLIC, 0, diffuse)
            .Read(2, SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS, 0, normal)
            .Read(2, SET2_BINDING_GBUFFER_EMISSION, 0, emission)
            .Read(2, SET2_BINDING_GBUFFER_GEOMETRIC_NORMAL_AO, 0, geometricNormal)
            .Read(2, SET2_BINDING_GBUFFER_SPECULAR_F0_ROUGHNESS, 0, materialParams)
            .Read(2, SET2_BINDING_GBUFFER_IOR, 0, ior)
            .ReadWrite(2, SET2_BINDING_FINAL_COLOR, 0, outColor)
            .Execute([&](RDGPassContext context) {

                DeferredLightingSetting newSetting = setting;
                if (EngineContext::Render()->IsPassEnabled(RESTIR_GI_PASS))
                    newSetting.directOnly = 1;

                RHICommandListRef command = context.command;
                command->SetComputePipeline(computePipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->PushConstants(&newSetting, sizeof(DeferredLightingSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16),
                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16),
                    1);
            })
            .Finish();
    }
}
