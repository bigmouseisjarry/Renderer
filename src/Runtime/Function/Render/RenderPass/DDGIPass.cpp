#include "DDGIPass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <cstdint>
#include <string>

void DDGIPass::Init()
{
    auto backend = EngineContext::RHI();

    rayGenShader     = Shader(EngineContext::File()->ShaderPath() + "ddgi/ddgi_trace.rgen.spv", SHADER_FREQUENCY_RAY_GEN);
    rayMissShader    = Shader(EngineContext::File()->ShaderPath() + "ddgi/ddgi_trace.rmiss.spv", SHADER_FREQUENCY_RAY_MISS);
    closestHitShader = Shader(EngineContext::File()->ShaderPath() + "ddgi/ddgi_trace.rchit.spv", SHADER_FREQUENCY_CLOSEST_HIT);

    computeShader[0] = Shader(EngineContext::File()->ShaderPath() + "ddgi/ddgi_radiance.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[1] = Shader(EngineContext::File()->ShaderPath() + "ddgi/ddgi_irradiance_probe_update.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[2] = Shader(EngineContext::File()->ShaderPath() + "ddgi/ddgi_depth_probe_update.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[3] = Shader(EngineContext::File()->ShaderPath() + "ddgi/ddgi_irradiance_border_update.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[4] = Shader(EngineContext::File()->ShaderPath() + "ddgi/ddgi_depth_border_update.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIShaderBindingTableInfo sbtInfo = {};
    sbtInfo.AddRayGenGroup(rayGenShader.shader);
    sbtInfo.AddHitGroup(closestHitShader.shader, nullptr, nullptr);
    sbtInfo.AddMissGroup(rayMissShader.shader);
    RHIShaderBindingTableRef sbt = EngineContext::RHI()->CreateShaderBindingTable(sbtInfo);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo())       
                                                                                                                                                                                                                                                                                                      .AddPushConstant({128, SHADER_FREQUENCY_ALL});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    {
        RHIRayTracingPipelineInfo pipelineInfo  = {};
        pipelineInfo.shaderBindingTable = sbt;
        pipelineInfo.rootSignature = rootSignature;
        rayTracingPipeline   = backend->CreateRayTracingPipeline(pipelineInfo);
    }

    {
        RHIComputePipelineInfo pipelineInfo = {};
        pipelineInfo.rootSignature = rootSignature;

        for(int i = 0; i < 5; i++)
        {
            pipelineInfo.computeShader = computeShader[i].shader;
            computePipeline[i] = backend->CreateComputePipeline(pipelineInfo);
        }
    }
}   

void DDGIPass::Build(RDGBuilder& builder)
{
    if( IsEnabled() &&
        // !EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) &&
        // !EngineContext::Render()->IsPassEnabled(RAY_TRACING_BASE_PASS) &&    //Surface cache有使用
        !EngineContext::Render()->IsPassEnabled(PATH_TRACING_PASS))
    {
        volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()] = EngineContext::Render()->GetLightManager()->GetVolumeLights();

        for (uint32_t i = 0; i < volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()].size(); i++)
		{
            auto& volumeLight = volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()][i];

            std::string index = " [" + std::to_string(volumeLight->GetVolumeLightID()) + "]";

            RDGTextureHandle diffuseTex = builder.CreateTexture(GetName() + " G-Buffer Diffuse/Metallic " + index)
                .Import(volumeLight->GetTextres().diffuseTex->texture)
                .Finish();

            RDGTextureHandle normalTex = builder.CreateTexture(GetName() + " G-Buffer Normal/Roughness " + index)
                .Import(volumeLight->GetTextres().normalTex->texture)
                .Finish();

            RDGTextureHandle emissionTex = builder.CreateTexture(GetName() + " G-Buffer Emission " + index)
                .Import(volumeLight->GetTextres().emissionTex->texture)
                .Finish();

            RDGTextureHandle positionTex = builder.CreateTexture(GetName() + " G-Buffer Position " + index)
                .Import(volumeLight->GetTextres().positionTex->texture)
                .Finish();

            RDGTextureHandle radianceTex = builder.CreateTexture(GetName() + " Radiance " + index)
                .Import(volumeLight->GetTextres().radianceTex->texture)
                .Finish();

            // 名字与消费者严格一致（DDGI ProbeRelocate/DeferredLighting/SurfaceCache用单空格形式）：
            // index自带前导空格，多拼一个空格=双名字双节点同物理图（物理守卫2026-10-26定罪）
            RDGTextureHandle irradianceTex = builder.GetOrCreateTexture("Volume Light Irradiance" + index)
                .Import(volumeLight->GetTextres().irradianceTex->texture)
                .Finish();

            RDGTextureHandle depthTex = builder.GetOrCreateTexture("Volume Light Depth" + index)
                .Import(volumeLight->GetTextres().depthTex->texture)
                .Finish();

            // 光追更新volumeLight的G-Buffer
			if (volumeLight->Enable() &&
				volumeLight->ShouldUpdate(0))     
			{
                RDGRayTracingPassHandle pass = builder.CreateRayTracingPass(GetName() + " Trace Ray" + index)
                    .PassIndex(i)
                    .RootSignature(rootSignature)
                    .Shaders({rayGenShader.shader, rayMissShader.shader, closestHitShader.shader})
                    .ReadAccelerationStructure(builder.GetBuffer("TLAS Storage"))
                    .ReadWrite(2, SET2_BINDING_DDGI_TRACE_ALBEDO_METALLIC, 0, diffuseTex)
                    .ReadWrite(2, SET2_BINDING_DDGI_TRACE_NORMAL_ROUGHNESS, 0, normalTex)
                    .ReadWrite(2, SET2_BINDING_DDGI_TRACE_EMISSION, 0, emissionTex)
                    .ReadWrite(2, SET2_BINDING_DDGI_TRACE_POSITION, 0, positionTex)
                    .Execute([&](RDGPassContext context) {

                        auto& volumeLight = volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()][context.passIndex[0]];

                        DDGIComputeSetting setting = {};
                        setting.volumeLightID = volumeLight->GetVolumeLightID();

                        RHICommandListRef command = context.command; 
                        command->SetRayTracingPipeline(rayTracingPipeline);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);  
                        command->BindDescriptorSet(context.descriptors[2], 2);
                        command->PushConstants(&setting, sizeof(DDGIComputeSetting), SHADER_FREQUENCY_ALL);
                        command->TraceRays( volumeLight->GetRaysPerProbe(), 
                                            volumeLight->GetProbeCounts().x() * volumeLight->GetProbeCounts().y() * volumeLight->GetProbeCounts().z(), 
                                            1);
                    })
                    .Finish();
            }    	

            // 更新光照
            if (volumeLight->Enable() &&
				volumeLight->ShouldUpdate(1))
            {   
                auto pass0 = builder.CreateComputePass(GetName() + " Radiance" + index)
                    .AddFlag(RDGPassFlags::ForceGraphicsQueue)
                    .PassIndex(i)
                    .RootSignature(rootSignature)
                    .Shaders({computeShader[0].shader})
                    .ReadWrite(2, SET2_BINDING_DDGI_TRACE_ALBEDO_METALLIC, 0, diffuseTex)
                    .ReadIfValid(2, SET2_BINDING_DIRECTIONAL_SHADOW, 0, "Directional Depth [0]", VIEW_TYPE_2D, { TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1})
                    .ReadIfValid(2, SET2_BINDING_DIRECTIONAL_SHADOW, 1, "Directional Depth [1]", VIEW_TYPE_2D, { TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1})
                    .ReadIfValid(2, SET2_BINDING_DIRECTIONAL_SHADOW, 2, "Directional Depth [2]", VIEW_TYPE_2D, { TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1})
                    .ReadIfValid(2, SET2_BINDING_DIRECTIONAL_SHADOW, 3, "Directional Depth [3]", VIEW_TYPE_2D, { TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1})
                    .ReadIfValid(2, SET2_BINDING_POINT_SHADOW, 0, "Point Shadow Filtered Color [0]", VIEW_TYPE_CUBE, { TEXTURE_ASPECT_COLOR, 0, 1, 0, 6})
                    .ReadIfValid(2, SET2_BINDING_POINT_SHADOW, 1, "Point Shadow Filtered Color [1]", VIEW_TYPE_CUBE, { TEXTURE_ASPECT_COLOR, 0, 1, 0, 6})
                    .ReadWrite(2, SET2_BINDING_DDGI_TRACE_NORMAL_ROUGHNESS, 0, normalTex)
                    .ReadWrite(2, SET2_BINDING_DDGI_TRACE_EMISSION, 0, emissionTex)
                    .ReadWrite(2, SET2_BINDING_DDGI_TRACE_POSITION, 0, positionTex)
                    .ReadWrite(2, SET2_BINDING_DDGI_RADIANCE, 0, radianceTex);

                for(auto& vl : EngineContext::Render()->GetLightManager()->GetVolumeLights())
                {
                    uint32_t vid = vl->GetVolumeLightID();
                    RDGTextureHandle irr = builder.GetOrCreateTexture("Volume Light Irradiance [" + std::to_string(vid) + "]")
                        .Import(vl->GetTextres().irradianceTex->texture).Finish();
                    RDGTextureHandle dep = builder.GetOrCreateTexture("Volume Light Depth [" + std::to_string(vid) + "]")
                        .Import(vl->GetTextres().depthTex->texture).Finish();
                    pass0.Read(2, SET2_BINDING_VOLUME_LIGHT_IRRADIANCE, vid, irr, VIEW_TYPE_2D, {});
                    pass0.Read(2, SET2_BINDING_VOLUME_LIGHT_DEPTH, vid, dep, VIEW_TYPE_2D, {});
                }
                pass0
                    .Execute([&](RDGPassContext context) {
                        
                        auto& volumeLight = volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()][context.passIndex[0]];

                        DDGIComputeSetting setting = {};
                        setting.volumeLightID = volumeLight->GetVolumeLightID();

                        RHICommandListRef command = context.command; 
                        command->SetComputePipeline(computePipeline[0]);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);  
                        command->BindDescriptorSet(context.descriptors[2], 2);
                        command->PushConstants(&setting, sizeof(DDGIComputeSetting), SHADER_FREQUENCY_ALL);
                        command->Dispatch(volumeLight->GetRaysPerProbe() / 32,
                            std::ceil(volumeLight->GetProbeCounts().x() * volumeLight->GetProbeCounts().y() * volumeLight->GetProbeCounts().z() / 32),
                            1);
                    })
                    .Finish();
                

                RDGComputePassHandle pass1 = builder.CreateComputePass(GetName() + " Irradiance Probe Update" + index)
                    .AddFlag(RDGPassFlags::ForceGraphicsQueue)
                    .PassIndex(i)
                    .RootSignature(rootSignature)
                    .Shaders({computeShader[1].shader})
                    .ReadWrite(2, SET2_BINDING_DDGI_TRACE_POSITION, 0, positionTex)
                    .ReadWrite(2, SET2_BINDING_DDGI_RADIANCE, 0, radianceTex)
                    .ReadWrite(2, SET2_BINDING_DDGI_IRRADIANCE, 0, irradianceTex)
                    .Execute([&](RDGPassContext context) {
                        
                        auto& volumeLight = volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()][context.passIndex[0]];

                        DDGIComputeSetting setting = {};
                        setting.volumeLightID = volumeLight->GetVolumeLightID();

                        RHICommandListRef command = context.command; 
                        command->SetComputePipeline(computePipeline[1]);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);  
                        command->BindDescriptorSet(context.descriptors[2], 2);
                        command->PushConstants(&setting, sizeof(DDGIComputeSetting), SHADER_FREQUENCY_ALL);
                        command->Dispatch(  volumeLight->GetProbeCounts().x() * volumeLight->GetProbeCounts().y(), 
                                            volumeLight->GetProbeCounts().z(), 
                                            1);  
                    })
                    .Finish();

                RDGComputePassHandle pass2 = builder.CreateComputePass(GetName() + " Depth Probe Update" + index)
                    .AddFlag(RDGPassFlags::ForceGraphicsQueue)
                    .PassIndex(i)
                    .RootSignature(rootSignature)
                    .Shaders({computeShader[2].shader})
                    .ReadWrite(2, SET2_BINDING_DDGI_TRACE_POSITION, 0, positionTex)
                    .ReadWrite(2, SET2_BINDING_DDGI_DEPTH, 0, depthTex)
                    .Execute([&](RDGPassContext context) {
                        
                        auto& volumeLight = volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()][context.passIndex[0]];

                        DDGIComputeSetting setting = {};
                        setting.volumeLightID = volumeLight->GetVolumeLightID();

                        RHICommandListRef command = context.command; 
                        command->SetComputePipeline(computePipeline[2]);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);  
                        command->BindDescriptorSet(context.descriptors[2], 2);
                        command->PushConstants(&setting, sizeof(DDGIComputeSetting), SHADER_FREQUENCY_ALL);
                        command->Dispatch(  volumeLight->GetProbeCounts().x() * volumeLight->GetProbeCounts().y(), 
                                            volumeLight->GetProbeCounts().z(), 
                                            1);  
                    })
                    .Finish();

                RDGComputePassHandle pass3 = builder.CreateComputePass(GetName() + " Irradiance Border Update" + index)
                    .AddFlag(RDGPassFlags::ForceGraphicsQueue)
                    .PassIndex(i)
                    .RootSignature(rootSignature)
                    .Shaders({computeShader[3].shader})
                    .ReadWrite(2, SET2_BINDING_DDGI_IRRADIANCE, 0, irradianceTex)
                    .Execute([&](RDGPassContext context) {       
                        
                        auto& volumeLight = volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()][context.passIndex[0]];

                        DDGIComputeSetting setting = {};
                        setting.volumeLightID = volumeLight->GetVolumeLightID();

                        RHICommandListRef command = context.command; 
                        command->SetComputePipeline(computePipeline[3]);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);  
                        command->BindDescriptorSet(context.descriptors[2], 2);
                        command->PushConstants(&setting, sizeof(DDGIComputeSetting), SHADER_FREQUENCY_ALL);
                        command->Dispatch(  volumeLight->GetProbeCounts().x() * volumeLight->GetProbeCounts().y(), 
                                            volumeLight->GetProbeCounts().z(), 
                                            1);  
                    })
                    .Finish();

                RDGComputePassHandle pass4 = builder.CreateComputePass(GetName() + " Depth Border Update" + index)
                    .AddFlag(RDGPassFlags::ForceGraphicsQueue)
                    .PassIndex(i)
                    .RootSignature(rootSignature)
                    .Shaders({computeShader[4].shader})
                    .ReadWrite(2, SET2_BINDING_DDGI_DEPTH, 0, depthTex)
                    .Execute([&](RDGPassContext context) {       
                        
                        auto& volumeLight = volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()][context.passIndex[0]];

                        DDGIComputeSetting setting = {};
                        setting.volumeLightID = volumeLight->GetVolumeLightID();

                        RHICommandListRef command = context.command; 
                        command->SetComputePipeline(computePipeline[4]);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);  
                        command->BindDescriptorSet(context.descriptors[2], 2);
                        command->PushConstants(&setting, sizeof(DDGIComputeSetting), SHADER_FREQUENCY_ALL);
                        command->Dispatch(  volumeLight->GetProbeCounts().x() * volumeLight->GetProbeCounts().y(), 
                                            volumeLight->GetProbeCounts().z(), 
                                            1);  
                    })
                    .OutputRead(irradianceTex)
                    .OutputRead(depthTex)
                    .Finish();
            }
		}
    }
}
