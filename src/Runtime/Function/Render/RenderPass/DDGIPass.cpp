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
                     .AddEntry({1, 0, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 1, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 2, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 3, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 4, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 5, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 6, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})
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
                .Import(volumeLight->GetTextres().diffuseTex->texture, RESOURCE_STATE_UNDEFINED)
                .Finish();

            RDGTextureHandle normalTex = builder.CreateTexture(GetName() + " G-Buffer Normal/Roughness " + index)
                .Import(volumeLight->GetTextres().normalTex->texture, RESOURCE_STATE_UNDEFINED)
                .Finish();

            RDGTextureHandle emissionTex = builder.CreateTexture(GetName() + " G-Buffer Emission " + index)
                .Import(volumeLight->GetTextres().emissionTex->texture, RESOURCE_STATE_UNDEFINED)
                .Finish();

            RDGTextureHandle positionTex = builder.CreateTexture(GetName() + " G-Buffer Position " + index)
                .Import(volumeLight->GetTextres().positionTex->texture, RESOURCE_STATE_UNDEFINED)
                .Finish();

            RDGTextureHandle radianceTex = builder.CreateTexture(GetName() + " Radiance " + index)
                .Import(volumeLight->GetTextres().radianceTex->texture, RESOURCE_STATE_UNDEFINED)
                .Finish();

            RDGTextureHandle irradianceTex = builder.CreateTexture(GetName() + " Irradiance " + index)
                .Import(volumeLight->GetTextres().irradianceTex->texture, RESOURCE_STATE_UNDEFINED)
                .Finish();

            RDGTextureHandle depthTex = builder.CreateTexture(GetName() + " Depth " + index)
                .Import(volumeLight->GetTextres().depthTex->texture, RESOURCE_STATE_UNDEFINED)
                .Finish();

            // 光追更新G-Buffer
			if (volumeLight->Enable() &&
				volumeLight->ShouldUpdate(0))
			{		
                RDGRayTracingPassHandle pass = builder.CreateRayTracingPass(GetName() + " Trace Ray" + index)
                    .PassIndex(i)
                    .RootSignature(rootSignature)
                    .ReadWrite(1, 0, 0, diffuseTex)
                    .ReadWrite(1, 1, 0, normalTex)
                    .ReadWrite(1, 2, 0, emissionTex)
                    .ReadWrite(1, 3, 0, positionTex)
                    .ReadWrite(1, 4, 0, radianceTex)
                    .ReadWrite(1, 5, 0, irradianceTex)
                    .ReadWrite(1, 6, 0, depthTex)
                    .Execute([&](RDGPassContext context) {       
                        
                        auto& volumeLight = volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()][context.passIndex[0]];

                        DDGIComputeSetting setting = {};
                        setting.volumeLightID = volumeLight->GetVolumeLightID();

                        RHICommandListRef command = context.command; 
                        command->SetRayTracingPipeline(rayTracingPipeline);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);  
                        command->BindDescriptorSet(context.descriptors[1], 1);
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
                RDGComputePassHandle pass0 = builder.CreateComputePass(GetName() + " Radiance" + index)
                    .PassIndex(i)
                    .RootSignature(rootSignature)
                    .ReadWrite(1, 0, 0, diffuseTex)
                    .ReadWrite(1, 1, 0, normalTex)
                    .ReadWrite(1, 2, 0, emissionTex)
                    .ReadWrite(1, 3, 0, positionTex)
                    .ReadWrite(1, 4, 0, radianceTex)
                    .ReadWrite(1, 5, 0, irradianceTex)
                    .ReadWrite(1, 6, 0, depthTex)
                    .Execute([&](RDGPassContext context) {       
                        
                        auto& volumeLight = volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()][context.passIndex[0]];

                        DDGIComputeSetting setting = {};
                        setting.volumeLightID = volumeLight->GetVolumeLightID();

                        RHICommandListRef command = context.command; 
                        command->SetComputePipeline(computePipeline[0]);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);  
                        command->BindDescriptorSet(context.descriptors[1], 1);
                        command->PushConstants(&setting, sizeof(DDGIComputeSetting), SHADER_FREQUENCY_ALL);
                        command->Dispatch(  volumeLight->GetRaysPerProbe(), 
                                            volumeLight->GetProbeCounts().x() * volumeLight->GetProbeCounts().y() * volumeLight->GetProbeCounts().z(), 
                                            1); 
                    })
                    .Finish();


                RDGComputePassHandle pass1 = builder.CreateComputePass(GetName() + " Irradiance Probe Update" + index)
                    .PassIndex(i)
                    .RootSignature(rootSignature)
                    .ReadWrite(1, 0, 0, diffuseTex)
                    .ReadWrite(1, 1, 0, normalTex)
                    .ReadWrite(1, 2, 0, emissionTex)
                    .ReadWrite(1, 3, 0, positionTex)
                    .ReadWrite(1, 4, 0, radianceTex)
                    .ReadWrite(1, 5, 0, irradianceTex)
                    .ReadWrite(1, 6, 0, depthTex)
                    .Execute([&](RDGPassContext context) {       
                        
                        auto& volumeLight = volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()][context.passIndex[0]];

                        DDGIComputeSetting setting = {};
                        setting.volumeLightID = volumeLight->GetVolumeLightID();

                        RHICommandListRef command = context.command; 
                        command->SetComputePipeline(computePipeline[1]);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);  
                        command->BindDescriptorSet(context.descriptors[1], 1);
                        command->PushConstants(&setting, sizeof(DDGIComputeSetting), SHADER_FREQUENCY_ALL);
                        command->Dispatch(  volumeLight->GetProbeCounts().x() * volumeLight->GetProbeCounts().y(), 
                                            volumeLight->GetProbeCounts().z(), 
                                            1);  
                    })
                    .Finish();

                RDGComputePassHandle pass2 = builder.CreateComputePass(GetName() + " Depth Probe Update" + index)
                    .PassIndex(i)
                    .RootSignature(rootSignature)
                    .ReadWrite(1, 0, 0, diffuseTex)
                    .ReadWrite(1, 1, 0, normalTex)
                    .ReadWrite(1, 2, 0, emissionTex)
                    .ReadWrite(1, 3, 0, positionTex)
                    .ReadWrite(1, 4, 0, radianceTex)
                    .ReadWrite(1, 5, 0, irradianceTex)
                    .ReadWrite(1, 6, 0, depthTex)
                    .Execute([&](RDGPassContext context) {       
                        
                        auto& volumeLight = volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()][context.passIndex[0]];

                        DDGIComputeSetting setting = {};
                        setting.volumeLightID = volumeLight->GetVolumeLightID();

                        RHICommandListRef command = context.command; 
                        command->SetComputePipeline(computePipeline[2]);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);  
                        command->BindDescriptorSet(context.descriptors[1], 1);
                        command->PushConstants(&setting, sizeof(DDGIComputeSetting), SHADER_FREQUENCY_ALL);
                        command->Dispatch(  volumeLight->GetProbeCounts().x() * volumeLight->GetProbeCounts().y(), 
                                            volumeLight->GetProbeCounts().z(), 
                                            1);  
                    })
                    .Finish();

                RDGComputePassHandle pass3 = builder.CreateComputePass(GetName() + " Irradiance Border Update" + index)
                    .PassIndex(i)
                    .RootSignature(rootSignature)
                    .ReadWrite(1, 0, 0, diffuseTex)
                    .ReadWrite(1, 1, 0, normalTex)
                    .ReadWrite(1, 2, 0, emissionTex)
                    .ReadWrite(1, 3, 0, positionTex)
                    .ReadWrite(1, 4, 0, radianceTex)
                    .ReadWrite(1, 5, 0, irradianceTex)
                    .ReadWrite(1, 6, 0, depthTex)
                    .Execute([&](RDGPassContext context) {       
                        
                        auto& volumeLight = volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()][context.passIndex[0]];

                        DDGIComputeSetting setting = {};
                        setting.volumeLightID = volumeLight->GetVolumeLightID();

                        RHICommandListRef command = context.command; 
                        command->SetComputePipeline(computePipeline[3]);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);  
                        command->BindDescriptorSet(context.descriptors[1], 1);
                        command->PushConstants(&setting, sizeof(DDGIComputeSetting), SHADER_FREQUENCY_ALL);
                        command->Dispatch(  volumeLight->GetProbeCounts().x() * volumeLight->GetProbeCounts().y(), 
                                            volumeLight->GetProbeCounts().z(), 
                                            1);  
                    })
                    .Finish();

                RDGComputePassHandle pass4 = builder.CreateComputePass(GetName() + " Depth Border Update" + index)
                    .PassIndex(i)
                    .RootSignature(rootSignature)
                    .ReadWrite(1, 0, 0, diffuseTex)
                    .ReadWrite(1, 1, 0, normalTex)
                    .ReadWrite(1, 2, 0, emissionTex)
                    .ReadWrite(1, 3, 0, positionTex)
                    .ReadWrite(1, 4, 0, radianceTex)
                    .ReadWrite(1, 5, 0, irradianceTex)
                    .ReadWrite(1, 6, 0, depthTex)
                    .Execute([&](RDGPassContext context) {       
                        
                        auto& volumeLight = volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()][context.passIndex[0]];

                        DDGIComputeSetting setting = {};
                        setting.volumeLightID = volumeLight->GetVolumeLightID();

                        RHICommandListRef command = context.command; 
                        command->SetComputePipeline(computePipeline[4]);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);  
                        command->BindDescriptorSet(context.descriptors[1], 1);
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
