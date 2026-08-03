#include "SurfaceCachePass.h"
#include "Core/Math/Math.h"
#include "Core/SurfaceCache/SurfaceCache.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGBuilder.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderPass/RenderPass.h"
#include <algorithm>
#include <cstdint>
#include <string>

#include <vector>

void SurfaceCachePass::Init()
{
    auto backend = EngineContext::RHI();

    vertexShader    = Shader(EngineContext::File()->ShaderPath() + "surface_cache/rasterize.vert.spv", SHADER_FREQUENCY_VERTEX);
    fragmentShader  = Shader(EngineContext::File()->ShaderPath() + "surface_cache/rasterize.frag.spv", SHADER_FREQUENCY_FRAGMENT);
    computeShader   = Shader(EngineContext::File()->ShaderPath() + "surface_cache/direct_lighting.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry({1, 0, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})      // CACHE_DIFFUSE_METALLIC
                     .AddEntry({1, 1, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})      // CACHE_NORMAL_ROUGHNESS
                     .AddEntry({1, 2, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})      // CACHE_EMISSION
                     .AddEntry({1, 3, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_TEXTURE})   // CACHE_LIGHTING
                     .AddEntry({1, 4, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_TEXTURE})      // CACHE_DEPTH
                     .AddEntry({1, 5, 1, SHADER_FREQUENCY_ALL, RESOURCE_TYPE_RW_BUFFER})    // CACHE_DIRECT_LIGHTING_DISPATCH
                     .AddPushConstant({128, SHADER_FREQUENCY_GRAPHICS});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);


    {
        RHIGraphicsPipelineInfo pipelineInfo    = {};
        pipelineInfo.vertexShader               = vertexShader.shader;
        pipelineInfo.fragmentShader             = fragmentShader.shader;
        pipelineInfo.primitiveType              = PRIMITIVE_TYPE_TRIANGLE_LIST;
        pipelineInfo.rasterizerState            = { FILL_MODE_SOLID, CULL_MODE_BACK, DEPTH_CLIP, 0.0f, 0.0f };                    
        pipelineInfo.depthStencilState          = { COMPARE_FUNCTION_LESS_EQUAL, true, true };

        pipelineInfo.rootSignature                  = rootSignature;
        for(uint32_t i = 0; i < 3; i++) pipelineInfo.blendState.renderTargets[i].enable = false;
        pipelineInfo.colorAttachmentFormats[0]      = FORMAT_R8G8B8A8_UNORM;
        pipelineInfo.colorAttachmentFormats[1]      = FORMAT_R8G8B8A8_SNORM;
        pipelineInfo.colorAttachmentFormats[2]      = FORMAT_R16G16B16A16_SFLOAT;                                            
        pipelineInfo.depthStencilAttachmentFormat   = EngineContext::Render()->GetDepthFormat();
        graphicsPipeline                            = EngineContext::RHI()->CreateGraphicsPipeline(pipelineInfo);   
    }
    {
        RHIComputePipelineInfo pipelineInfo     = {};
        pipelineInfo.rootSignature              = rootSignature;
        pipelineInfo.computeShader              = computeShader.shader;
        computePipeline                         = backend->CreateComputePipeline(pipelineInfo);
    }

    for(int i = 0; i < 4; i++)
    {
        ClearAttachment clearAttachment = {};
        clearAttachment.binding = i;
        clearAttachment.clearColor.r = i < 3 ? 0.0f : 1.0f;
        clearAttachment.clearColor.g = 0.0f;
        clearAttachment.clearColor.b = 0.0f;
        clearAttachment.clearColor.a = 0.0f;
        clearAttachment.aspect = i < 3 ? TEXTURE_ASPECT_COLOR : TEXTURE_ASPECT_DEPTH;
        clearAttachments.push_back(clearAttachment);
    }
}   

void SurfaceCachePass::Build(RDGBuilder& builder) 
{
    //if (IsEnabled())
    {
        RDGTextureHandle diffuseTex = builder.CreateTexture("Surface Cache Diffuse/Metallic")
            .Import(EngineContext::RenderResource()->GetSurfaceCacheTexture(0)->texture, RESOURCE_STATE_UNDEFINED)
            .Finish();

        RDGTextureHandle normalTex = builder.CreateTexture("Surface Cache Normal/Roughness")
            .Import(EngineContext::RenderResource()->GetSurfaceCacheTexture(1)->texture, RESOURCE_STATE_UNDEFINED)
            .Finish();

        RDGTextureHandle emissionTex = builder.CreateTexture("Surface Cache Emission")
            .Import(EngineContext::RenderResource()->GetSurfaceCacheTexture(2)->texture, RESOURCE_STATE_UNDEFINED)
            .Finish();

        RDGTextureHandle lightingTex = builder.CreateTexture("Surface Cache Lighting")
            .Import(EngineContext::RenderResource()->GetSurfaceCacheTexture(3)->texture, RESOURCE_STATE_UNDEFINED)
            .Finish();

        RDGTextureHandle depthTex = builder.CreateTexture("Surface Cache Depth")
            .Import(EngineContext::RenderResource()->GetSurfaceCacheTexture(4)->texture, RESOURCE_STATE_UNDEFINED)
            .Finish();

        RDGBufferHandle directLightingBuf = builder.CreateBuffer("Surface Cache Direct Lighting Buffer")
            .Import(directLightingBuffer[EngineContext::ThreadPool()->ThreadFrameIndex()].buffer, RESOURCE_STATE_UNDEFINED)
            .Finish();

        RDGRenderPassHandle pass0 = builder.CreateRenderPass(GetName() + " Rasterize")
            .Color(0, diffuseTex, !init ? ATTACHMENT_LOAD_OP_CLEAR : ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
            .Color(1, normalTex, !init ? ATTACHMENT_LOAD_OP_CLEAR : ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
            .Color(2, emissionTex, !init ? ATTACHMENT_LOAD_OP_CLEAR : ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
            .DepthStencil(depthTex, !init ? ATTACHMENT_LOAD_OP_CLEAR : ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, 1.0f, 0)
            .Execute([&](RDGPassContext context) {

                RHICommandListRef command = context.command;    
                command->SetGraphicsPipeline(graphicsPipeline);                                        
                command->SetDepthBias(0.0f, 0.0f, 0.0f);          
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);   
                
                command->ClearScissors(clearAttachments, EngineContext::Render()->GetSurfaceCacheManager()->GetClearScissors());

                for(auto& draw : EngineContext::Render()->GetSurfaceCacheManager()->GetRasterizeDraws())
                {
                    SurfaceCacheRasterizeSetting setting = {};
                    setting.meshCardID = draw.task.meshCardID;
                    setting.objectID = draw.task.objectID;
                    setting.vertexID = draw.task.vertexBuffer->vertexID;

                    Offset2D viewportMin = Offset2D(draw.atlasOffset.x(), draw.atlasOffset.y());
                    Offset2D viewportMax = Offset2D(draw.atlasExtent.x(), draw.atlasExtent.y()) + viewportMin;

                    command->SetViewport(viewportMin, viewportMax);
                    command->SetScissor(viewportMin, viewportMax); 
                    command->PushConstants(&setting, sizeof(SurfaceCacheRasterizeSetting), SHADER_FREQUENCY_GRAPHICS);

                    command->BindIndexBuffer(draw.task.indexBuffer->buffer);
                    command->DrawIndexed(draw.task.indexBuffer->IndexNum(), 1);   
                }
            })
            .Finish();

        RDGComputePassHandle pass1 = builder.CreateComputePass(GetName() + " Direct Lighting")
            .RootSignature(rootSignature)
            .Read(1, 0, 0, diffuseTex)
            .Read(1, 1, 0, normalTex)
            .Read(1, 2, 0, emissionTex)
            .ReadWrite(1, 3, 0, lightingTex)
            .Read(1, 4, 0, depthTex)
            .ReadWrite(1, 5, 0, directLightingBuf)
            .Execute([&](RDGPassContext context) {       
                
                auto& dispatches = EngineContext::Render()->GetSurfaceCacheManager()->GetDirectLightingDispatches();
                directLightingBuffer[EngineContext::ThreadPool()->ThreadFrameIndex()].SetData(dispatches);

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0); 
                command->BindDescriptorSet(context.descriptors[1], 1);
                command->Dispatch(  dispatches.size(), 
                                    1, 
                                    1);
            })
            .OutputRead(diffuseTex)
            .OutputRead(normalTex)
            .OutputRead(emissionTex)
            .OutputRead(lightingTex)
            .OutputRead(depthTex)
            .Finish();

        init = true;
    }
}
