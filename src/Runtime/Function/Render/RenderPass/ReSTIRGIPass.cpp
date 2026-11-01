#include "ReSTIRGIPass.h"
#include "Core/Math/Math.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderPass/RenderPass.h"
#include <cstdint>

#define NUM_NEIGHBORS 1

void ReSTIRGIPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader[0] = Shader(EngineContext::File()->ShaderPath() + "restir_gi/temporal_reuse.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[1] = Shader(EngineContext::File()->ShaderPath() + "restir_gi/spatial_reuse.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[2] = Shader(EngineContext::File()->ShaderPath() + "restir_gi/lighting.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo());      
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo compPipelineInfo     = {};
    compPipelineInfo.rootSignature              = rootSignature;

    compPipelineInfo.computeShader              = computeShader[0].shader;
    computePipeline[0] = backend->CreateComputePipeline(compPipelineInfo);

    compPipelineInfo.computeShader              = computeShader[1].shader;
    computePipeline[1] = backend->CreateComputePipeline(compPipelineInfo);

    compPipelineInfo.computeShader              = computeShader[2].shader;
    computePipeline[2] = backend->CreateComputePipeline(compPipelineInfo);
}   

void ReSTIRGIPass::Build(RDGBuilder& builder) 
{
    if( IsEnabled() && 
        !EngineContext::Render()->IsPassEnabled(PATH_TRACING_PASS))
    {
        currentIndex = (currentIndex + 1) % 2;

        Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

        RDGTextureHandle diffuse         = builder.GetTexture("G-Buffer Diffuse/Metallic");
        RDGTextureHandle normal          = builder.GetTexture("G-Buffer Normal/Roughness");
        RDGTextureHandle emission        = builder.GetTexture("G-Buffer Emission");
        RDGTextureHandle specularF0      = builder.GetTexture("G-Buffer Specular F0/Roughness");
        RDGTextureHandle reprojectionOut = builder.GetTexture("Reprojection Out");
        RDGTextureHandle outColor        = builder.GetTexture("Noisy HDR Color");

        RDGTextureHandle restirDiffuseColor = builder.GetOrCreateTexture("ReSTIR Diffuse Color") 
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .AllowRenderTarget()
            .Finish(); 

        RDGTextureHandle restirSpecularColor = builder.GetOrCreateTexture("ReSTIR Specular Color") 
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .AllowRenderTarget()
            .Finish();

        // buffer稳态均=UAV：三pass的ReadWrite，稳态由动态记载承接
        RDGBufferHandle resBuffer0 = builder.CreateBuffer("Reservoir Buffer 0")
            .Import(reservoirsBuffer[0].buffer, RESOURCE_STATE_UNORDERED_ACCESS)
            .Finish();

        RDGBufferHandle resBuffer1 = builder.CreateBuffer("Reservoir Buffer 1")
            .Import(reservoirsBuffer[1].buffer, RESOURCE_STATE_UNORDERED_ACCESS)
            .Finish();

        RDGBufferHandle resBuffer2 = builder.CreateBuffer("Reservoir Buffer 2")
            .Import(reservoirsBuffer[2].buffer, RESOURCE_STATE_UNORDERED_ACCESS)
            .Finish();

        RDGBufferHandle settingBuf = builder.GetOrCreateBuffer(GetName() + " Setting")
            .Import(restirSettingBuffer.buffer, RESOURCE_STATE_UNORDERED_ACCESS)
            .Finish();
        RDGBufferHandle reservoirA = currentIndex == 0 ? resBuffer1 : resBuffer2;
        RDGBufferHandle reservoirB = currentIndex == 0 ? resBuffer2 : resBuffer1;

        setting.combineMode = !(EngineContext::Render()->IsPassEnabled(SVGF_PASS) ||
                                EngineContext::Render()->IsPassEnabled(NRD_PASS)) ? 0 :
                               !EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) ? 1 : 2;
        restirSettingBuffer.SetData(setting);

        RDGBufferHandle cardUpdate = builder.GetOrCreateBuffer("Surface Cache Card Update")
            .Import(EngineContext::RenderResource()->GetCardUpdateBuffer(), RESOURCE_STATE_UNORDERED_ACCESS)
            .Finish();

        RDGComputePassHandle pass0 = builder.CreateComputePass(GetName() + " Temporal Reuse")
            .PassIndex(currentIndex)
            .RootSignature(rootSignature)
            .Shaders({computeShader[0].shader})           
            .ReadWrite(2, SET2_BINDING_MESH_CARD_READBACK, 0, cardUpdate)
            .Read(2, SET2_BINDING_GBUFFER_DIFFUSE_METALLIC, 0, diffuse)
            .Read(2, SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS, 0, normal)
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 1, builder.GetTexture("Previous Depth"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_SURFACE_CACHE, 0, builder.GetTexture("Surface Cache Diffuse/Metallic"))
            .Read(2, SET2_BINDING_SURFACE_CACHE, 1, builder.GetTexture("Surface Cache Normal/Roughness"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_SURFACE_CACHE, 2, builder.GetTexture("Surface Cache Emission"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_SURFACE_CACHE, 3, builder.GetTexture("Surface Cache Lighting"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_SURFACE_CACHE, 4, builder.GetTexture("Surface Cache Depth"), VIEW_TYPE_2D, {})
            .ReadWrite(2, SET2_BINDING_RESTIR_GI_RESERVOIRS, 0, resBuffer0)
            .ReadWrite(2, SET2_BINDING_RESTIR_GI_PREV_RESERVOIRS, 0, reservoirA)
            .Read(2, SET2_BINDING_GBUFFER_EMISSION, 0, emission)                  
            .Read(2, SET2_BINDING_REPROJECTION_RESULT, 0, reprojectionOut)        
            .ReadWrite(2, SET2_BINDING_RESTIR_GI_SETTING, 0, settingBuf)   
            .Execute([&](RDGPassContext context) {

                RHICommandListRef command = context.command;
                command->SetComputePipeline(computePipeline[0]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);  
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width / WIDTH_DOWNSAMPLE_RATE, 16),     
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height / HEIGHT_DOWNSAMPLE_RATE, 16), 
                                    1);
            })
            .Finish();

        RDGComputePassHandle pass1 = builder.CreateComputePass(GetName() + " Spatial Reuse")
            .PassIndex(currentIndex)
            .RootSignature(rootSignature)
            .Shaders({computeShader[1].shader})           
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))     
            .Read(2, SET2_BINDING_GBUFFER_DIFFUSE_METALLIC, 0, diffuse)
            .Read(2, SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS, 0, normal)
            .Read(2, SET2_BINDING_GBUFFER_EMISSION, 0, emission)
            .ReadWrite(2, SET2_BINDING_RESTIR_GI_SETTING, 0, settingBuf)
            .ReadWrite(2, SET2_BINDING_RESTIR_GI_RESERVOIRS, 0, resBuffer0)
            .ReadWrite(2, SET2_BINDING_RESTIR_GI_RESULT_RESERVOIRS, 0, reservoirB)
            .ReadAccelerationStructure(builder.GetBuffer("TLAS Storage"))
            .Execute([&](RDGPassContext context) {

                RHICommandListRef command = context.command;
                command->SetComputePipeline(computePipeline[1]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);  
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width / WIDTH_DOWNSAMPLE_RATE, 16),     
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height / HEIGHT_DOWNSAMPLE_RATE, 16), 
                                    1);
            })
            .Finish();

        RDGComputePassHandle pass2 = builder.CreateComputePass(GetName() + " Lighting")
            .PassIndex(currentIndex)
            .RootSignature(rootSignature)
            .Shaders({computeShader[2].shader})           
            .Read(2, SET2_BINDING_GBUFFER_DIFFUSE_METALLIC, 0, diffuse)
            .Read(2, SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS, 0, normal)
            .Read(2, SET2_BINDING_GBUFFER_EMISSION, 0, emission)
            .Read(2, SET2_BINDING_GBUFFER_SPECULAR_F0_ROUGHNESS, 0, specularF0)
            .ReadWrite(2, SET2_BINDING_RESTIR_GI_SETTING, 0, settingBuf)
            .ReadWrite(2, SET2_BINDING_RESTIR_GI_RESULT_RESERVOIRS, 0, reservoirB)
            .ReadWrite(2, SET2_BINDING_FINAL_COLOR, 0, outColor)
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 1, builder.GetTexture("Previous Depth"), VIEW_TYPE_2D, {})
            .ReadWrite(2, SET2_BINDING_GI_DIFFUSE_COLOR, 0, restirDiffuseColor)
            .ReadWrite(2, SET2_BINDING_GI_SPECULAR_COLOR, 0, restirSpecularColor)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[2]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();
    }
}
