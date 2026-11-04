#include "DeferredLightingPass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <memory>

void DeferredLightingPass::Init()
{
    auto backend = EngineContext::RHI();

    localShader        = Shader(EngineContext::File()->ShaderPath() + "deferred_lighting/deferred_lighting_local.comp.spv", SHADER_FREQUENCY_COMPUTE);
    directionalShader  = Shader(EngineContext::File()->ShaderPath() + "deferred_lighting/deferred_lighting_directional.comp.spv", SHADER_FREQUENCY_COMPUTE);
    sunShader          = Shader(EngineContext::File()->ShaderPath() + "deferred_lighting/deferred_lighting_sun.comp.spv", SHADER_FREQUENCY_COMPUTE);
    compositeShader    = Shader(EngineContext::File()->ShaderPath() + "deferred_lighting/deferred_lighting_composite.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo())
                     .AddPushConstant({sizeof(DeferredLightingSetting), SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;

    pipelineInfo.computeShader = localShader.shader;
    localPipeline   = backend->CreateComputePipeline(pipelineInfo);
    pipelineInfo.computeShader = directionalShader.shader;
    directionalPipeline   = backend->CreateComputePipeline(pipelineInfo);
    pipelineInfo.computeShader = sunShader.shader;
    sunPipeline     = backend->CreateComputePipeline(pipelineInfo);
    pipelineInfo.computeShader = compositeShader.shader;
    compositePipeline   = backend->CreateComputePipeline(pipelineInfo);
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
        // 链式RMW：①local覆盖写(local直接光+emission) → ②方向灯非太阳(条件声明) → ③sun(RMW) → ④composite(RMW终写)
        // 跳过逻辑与单体时代等价：三旗任一启用则全家不声明

        // 阶段① 局部灯块 //////////////////////////////////////////////////////////////////////
        builder.CreateComputePass(GetName() + " Local")
            .RootSignature(rootSignature)
            .Shaders({localShader.shader})
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))
            .ReadWrite(2, SET2_BINDING_TILE_LIGHTS, 0, builder.GetBuffer("Tile Lights"))
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

                Extent2D extent = EngineContext::Render()->GetWindowsExtent();
                RHICommandListRef command = context.command;
                command->SetComputePipeline(localPipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->PushConstants(&newSetting, sizeof(DeferredLightingSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(Math::CeilDivide(extent.width, 16), Math::CeilDivide(extent.height, 16), 1);
            })
            .Finish();

        // 阶段② 方向灯块非太阳 ////////////////////////////////////////////////////////////////
        // 现状唯一方向灯=太阳（sunSlot=0）由阶段③负责，本段恒不声明；
        // 非太阳方向灯随多方向光刀启用（directionalPipeline已在此养着）

        // 阶段③ 太阳 //////////////////////////////////////////////////////////////////////////
        auto dirLight = EngineContext::Render()->GetLightManager()->GetDirectionalLight();
        bool hasSun = dirLight && dirLight->Enable();
        if(hasSun)
        {
            builder.CreateComputePass(GetName() + " Sun")
                .RootSignature(rootSignature)
                .Shaders({sunShader.shader})
                .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))
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

                    Extent2D extent = EngineContext::Render()->GetWindowsExtent();
                    RHICommandListRef command = context.command;
                    command->SetComputePipeline(sunPipeline);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                    command->BindDescriptorSet(context.descriptors[2], 2);
                    command->PushConstants(&newSetting, sizeof(DeferredLightingSetting), SHADER_FREQUENCY_COMPUTE);
                    command->Dispatch(Math::CeilDivide(extent.width, 16), Math::CeilDivide(extent.height, 16), 1);
                })
                .Finish();
        }

        // 阶段④ 间接+合成 /////////////////////////////////////////////////////////////////////
        auto compositePass = builder.CreateComputePass(GetName() + " Composite")
            .RootSignature(rootSignature)
            .Shaders({compositeShader.shader})
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))
            .Read(2, SET2_BINDING_GBUFFER_DIFFUSE_METALLIC, 0, diffuse)
            .Read(2, SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS, 0, normal)
            .Read(2, SET2_BINDING_GBUFFER_EMISSION, 0, emission)
            .Read(2, SET2_BINDING_GBUFFER_GEOMETRIC_NORMAL_AO, 0, geometricNormal)
            .Read(2, SET2_BINDING_GBUFFER_SPECULAR_F0_ROUGHNESS, 0, materialParams)
            .Read(2, SET2_BINDING_GBUFFER_IOR, 0, ior)
            .ReadWrite(2, SET2_BINDING_FINAL_COLOR, 0, outColor);
        for(auto& volumeLight : EngineContext::Render()->GetLightManager()->GetVolumeLights())
        {
            uint32_t vid = volumeLight->GetVolumeLightID();
            RDGTextureHandle irr = builder.GetOrCreateTexture("Volume Light Irradiance [" + std::to_string(vid) + "]")
                .Import(volumeLight->GetTextres().irradianceTex->texture).Finish();
            RDGTextureHandle dep = builder.GetOrCreateTexture("Volume Light Depth [" + std::to_string(vid) + "]")
                .Import(volumeLight->GetTextres().depthTex->texture).Finish();
            compositePass.Read(2, SET2_BINDING_VOLUME_LIGHT_IRRADIANCE, vid, irr, VIEW_TYPE_2D, {});
            compositePass.Read(2, SET2_BINDING_VOLUME_LIGHT_DEPTH, vid, dep, VIEW_TYPE_2D, {});
        }
        compositePass
            .Execute([&](RDGPassContext context) {
                DeferredLightingSetting newSetting = setting;
                if (EngineContext::Render()->IsPassEnabled(RESTIR_GI_PASS))
                    newSetting.directOnly = 1;

                Extent2D extent = EngineContext::Render()->GetWindowsExtent();
                RHICommandListRef command = context.command;
                command->SetComputePipeline(compositePipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->PushConstants(&newSetting, sizeof(DeferredLightingSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(Math::CeilDivide(extent.width, 16), Math::CeilDivide(extent.height, 16), 1);
            })
            .Finish();
    }
}
