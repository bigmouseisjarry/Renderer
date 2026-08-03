#include "ReSTIRGIPass.h"
#include "Core/Math/Math.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderPass/RenderPass.h"
#include <cstdint>

#define NUM_NEIGHBORS 1
#define WIDTH_DOWNSAMPLE_RATE 1   
#define HEIGHT_DOWNSAMPLE_RATE 1    

void ReSTIRGIPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader[0] = Shader(EngineContext::File()->ShaderPath() + "restir_gi/temporal_reuse.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[1] = Shader(EngineContext::File()->ShaderPath() + "restir_gi/spatial_reuse.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[2] = Shader(EngineContext::File()->ShaderPath() + "restir_gi/lighting.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry({1, 0, 1, SHADER_FREQUENCY_RAY_TRACING | SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_BUFFER})     // RESTIR_DI_SETTING
                     .AddEntry({1, 1, 1, SHADER_FREQUENCY_RAY_TRACING | SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_BUFFER})     // RESERVOIRS
                     .AddEntry({1, 2, 1, SHADER_FREQUENCY_RAY_TRACING | SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_BUFFER})     // PREV_RESERVOIRS
                     .AddEntry({1, 3, 1, SHADER_FREQUENCY_RAY_TRACING | SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_BUFFER})     // RESULT_RESERVOIRS
                     .AddEntry({2, 0, 1, SHADER_FREQUENCY_RAY_TRACING | SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})    // G_BUFFER_DIFFUSE_METALLIC
                     .AddEntry({2, 1, 1, SHADER_FREQUENCY_RAY_TRACING | SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})    // G_BUFFER_NORMAL_ROUGHNESS
                     .AddEntry({2, 2, 1, SHADER_FREQUENCY_RAY_TRACING | SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})    // G_BUFFER_EMISSION
                     .AddEntry({2, 3, 1, SHADER_FREQUENCY_RAY_TRACING | SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})       // REPROJECTION_RESULT
                     .AddEntry({2, 4, 1, SHADER_FREQUENCY_RAY_TRACING | SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})    // FINAL_COLOR
                     .AddEntry({2, 5, 1, SHADER_FREQUENCY_RAY_TRACING | SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})    // GI_DIFFUSE_COLOR
                     .AddEntry({2, 6, 1, SHADER_FREQUENCY_RAY_TRACING | SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})    // GI_SPECULAR_COLOR
                     .AddPushConstant({128, SHADER_FREQUENCY_RAY_TRACING | SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    descriptorSet[0] = rootSignature->CreateDescriptorSet(1);
    descriptorSet[0]->UpdateDescriptor({.binding = 0, .index = 0, .resourceType = RESOURCE_TYPE_RW_BUFFER, .buffer = restirSettingBuffer.buffer});
    descriptorSet[0]->UpdateDescriptor({.binding = 1, .index = 0, .resourceType = RESOURCE_TYPE_RW_BUFFER, .buffer = reservoirsBuffer[0].buffer});
    descriptorSet[0]->UpdateDescriptor({.binding = 2, .index = 0, .resourceType = RESOURCE_TYPE_RW_BUFFER, .buffer = reservoirsBuffer[1].buffer});
    descriptorSet[0]->UpdateDescriptor({.binding = 3, .index = 0, .resourceType = RESOURCE_TYPE_RW_BUFFER, .buffer = reservoirsBuffer[2].buffer});

    descriptorSet[1] = rootSignature->CreateDescriptorSet(1);
    descriptorSet[1]->UpdateDescriptor({.binding = 0, .index = 0, .resourceType = RESOURCE_TYPE_RW_BUFFER, .buffer = restirSettingBuffer.buffer});
    descriptorSet[1]->UpdateDescriptor({.binding = 1, .index = 0, .resourceType = RESOURCE_TYPE_RW_BUFFER, .buffer = reservoirsBuffer[0].buffer});
    descriptorSet[1]->UpdateDescriptor({.binding = 2, .index = 0, .resourceType = RESOURCE_TYPE_RW_BUFFER, .buffer = reservoirsBuffer[2].buffer});
    descriptorSet[1]->UpdateDescriptor({.binding = 3, .index = 0, .resourceType = RESOURCE_TYPE_RW_BUFFER, .buffer = reservoirsBuffer[1].buffer});

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
        RDGTextureHandle reprojectionOut = builder.GetTexture("Reprojection Out");
        RDGTextureHandle outColor        = builder.GetTexture("Mesh Pass Out Color");

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

        RDGBufferHandle resBuffer0 = builder.CreateBuffer("Reservoir Buffer 0")
            .Import(reservoirsBuffer[0].buffer, RESOURCE_STATE_UNDEFINED)
            .Finish();

        RDGBufferHandle resBuffer1 = builder.CreateBuffer("Reservoir Buffer 1")
            .Import(reservoirsBuffer[1].buffer, RESOURCE_STATE_UNDEFINED)
            .Finish();

        RDGBufferHandle resBuffer2 = builder.CreateBuffer("Reservoir Buffer 2")
            .Import(reservoirsBuffer[2].buffer, RESOURCE_STATE_UNDEFINED)
            .Finish();

        setting.combineMode = !(EngineContext::Render()->IsPassEnabled(SVGF_PASS) ||
                                EngineContext::Render()->IsPassEnabled(NRD_PASS)) ? 0 : 
                               !EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) ? 1 : 2;
        restirSettingBuffer.SetData(setting);

        RDGComputePassHandle pass0 = builder.CreateComputePass(GetName() + " Temporal Reuse")
            .PassIndex(currentIndex)
            .RootSignature(rootSignature)
            .ReadWrite(2, 0, 0, diffuse)
            .ReadWrite(2, 1, 0, normal)
            .ReadWrite(2, 2, 0, emission)
            .Read(2, 3, 0, reprojectionOut)
            .ReadWrite(1, 1, 0, resBuffer0)
            .ReadWrite(1, 2, 0, resBuffer1)
            .ReadWrite(1, 3, 0, resBuffer2)
            .Execute([&](RDGPassContext context) {       
                
                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[0]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);  
                command->BindDescriptorSet(descriptorSet[context.passIndex[0]], 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width / WIDTH_DOWNSAMPLE_RATE, 16),     
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height / HEIGHT_DOWNSAMPLE_RATE, 16), 
                                    1);
            })
            .Finish();

        RDGComputePassHandle pass1 = builder.CreateComputePass(GetName() + " Spatial Reuse")
            .PassIndex(currentIndex)
            .RootSignature(rootSignature)
            .ReadWrite(2, 0, 0, diffuse)
            .ReadWrite(2, 1, 0, normal)
            .ReadWrite(2, 2, 0, emission)
            .Read(2, 3, 0, reprojectionOut)
            .ReadWrite(1, 1, 0, resBuffer0)
            .ReadWrite(1, 2, 0, resBuffer1)
            .ReadWrite(1, 3, 0, resBuffer2)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[1]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);  
                command->BindDescriptorSet(descriptorSet[context.passIndex[0]], 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width / WIDTH_DOWNSAMPLE_RATE, 16),     
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height / HEIGHT_DOWNSAMPLE_RATE, 16), 
                                    1);
            })
            .Finish();

        RDGComputePassHandle pass2 = builder.CreateComputePass(GetName() + " Lighting")
            .PassIndex(currentIndex)
            .RootSignature(rootSignature)
            .ReadWrite(2, 0, 0, diffuse)
            .ReadWrite(2, 1, 0, normal)
            .ReadWrite(2, 2, 0, emission)
            .Read(2, 3, 0, reprojectionOut)
            .ReadWrite(2, 4, 0, outColor)
            .ReadWrite(2, 5, 0, restirDiffuseColor)
            .ReadWrite(2, 6, 0, restirSpecularColor)
            .ReadWrite(1, 1, 0, resBuffer0)
            .ReadWrite(1, 2, 0, resBuffer1)
            .ReadWrite(1, 3, 0, resBuffer2)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[2]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(descriptorSet[context.passIndex[0]], 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .OutputReadWrite(resBuffer0)
            .OutputReadWrite(resBuffer1)
            .OutputReadWrite(resBuffer2)
            .Finish();
    }
}
