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
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo())       // 刀7b-3：set2全局统一RS
                                                                                                                                                                                                                  .AddPushConstant({128, SHADER_FREQUENCY_GRAPHICS}); // 掩码须与Execute里PushConstants的stage一致(VUID-01796要求命令掩码⊇range掩码)；direct_lighting.comp不用PC故无需COMPUTE
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
        // 五纹理稳态均=SRV：pass1(Direct Lighting)末尾全OutputRead收敛；lighting槽随后被SSSR/ReSTIRGI读亦SRV
        RDGTextureHandle diffuseTex = builder.CreateTexture("Surface Cache Diffuse/Metallic")
            .Import(EngineContext::RenderResource()->GetSurfaceCacheTexture(0)->texture)
            .Finish();

        RDGTextureHandle normalTex = builder.CreateTexture("Surface Cache Normal/Roughness")
            .Import(EngineContext::RenderResource()->GetSurfaceCacheTexture(1)->texture)
            .Finish();

        RDGTextureHandle emissionTex = builder.CreateTexture("Surface Cache Emission")
            .Import(EngineContext::RenderResource()->GetSurfaceCacheTexture(2)->texture)
            .Finish();

        RDGTextureHandle lightingTex = builder.CreateTexture("Surface Cache Lighting")
            .Import(EngineContext::RenderResource()->GetSurfaceCacheTexture(3)->texture)
            .Finish();

        RDGTextureHandle depthTex = builder.CreateTexture("Surface Cache Depth")
            .Import(EngineContext::RenderResource()->GetSurfaceCacheTexture(4)->texture)
            .Finish();

        // buffer稳态=UAV：pass1的ReadWrite后无收敛边
        RDGBufferHandle directLightingBuf = builder.CreateBuffer("Surface Cache Direct Lighting Buffer")
            .Import(directLightingBuffer[EngineContext::ThreadPool()->ThreadFrameIndex()].buffer, RESOURCE_STATE_UNORDERED_ACCESS)
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
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);   
                
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

        auto pass1 = builder.CreateComputePass(GetName() + " Direct Lighting")   // 刀5：auto持有builder——链中插volume light边循环需要继续链式调用
            .RootSignature(rootSignature)
            .Shaders({computeShader.shader})           // 刀7b-3：严格边校验
            .Read(2, SET2_BINDING_SC_CACHE_DIFFUSE, 0, diffuseTex)
            .Read(2, SET2_BINDING_SC_CACHE_NORMAL, 0, normalTex)
            .Read(2, SET2_BINDING_SC_CACHE_EMISSION, 0, emissionTex)
            .ReadWrite(2, SET2_BINDING_SC_CACHE_LIGHTING, 0, lightingTex)
            .Read(2, SET2_BINDING_SC_CACHE_DEPTH, 0, depthTex)
            .ReadWrite(2, SET2_BINDING_SC_DIRECT_LIGHTING_DISPATCH, 0, directLightingBuf, 0, 0);
            // 刀5：volume light经照明域b25/b26（原set0；index=volumeLightID动态对齐）
            // 自建导入（不依赖DDGI是否启用；GetOrCreate同参=与DDGI共享单节点）
            for(auto& volumeLight : EngineContext::Render()->GetLightManager()->GetVolumeLights())
            {
                uint32_t vid = volumeLight->GetVolumeLightID();
                RDGTextureHandle irr = builder.GetOrCreateTexture("Volume Light Irradiance [" + std::to_string(vid) + "]")
                    .Import(volumeLight->GetTextres().irradianceTex->texture).Finish();     // 刀6a：稳态=U4 OutputRead收尾（与DDGIPass同值，黑板名多导入点同值契约）
                RDGTextureHandle dep = builder.GetOrCreateTexture("Volume Light Depth [" + std::to_string(vid) + "]")
                    .Import(volumeLight->GetTextres().depthTex->texture).Finish();
                pass1.Read(2, SET2_BINDING_VOLUME_LIGHT_IRRADIANCE, vid, irr, VIEW_TYPE_2D, {});
                pass1.Read(2, SET2_BINDING_VOLUME_LIGHT_DEPTH, vid, dep, VIEW_TYPE_2D, {});
            }
            pass1
            // 刀DDGI假边扫描（2026-10-22）：探针实证direct_lighting.comp不吃DIRECTIONAL_SHADOW(b6)——
            // 卡片直接光=DDGI体积(b9/b10)+点光阴影(b7)，平行光四条ReadIfValid为假边已删
            .ReadIfValid(2, SET2_BINDING_POINT_SHADOW, 0, "Point Shadow Filtered Color [0]", VIEW_TYPE_CUBE, { TEXTURE_ASPECT_COLOR, 0, 1, 0, 6 })
            .ReadIfValid(2, SET2_BINDING_POINT_SHADOW, 1, "Point Shadow Filtered Color [1]", VIEW_TYPE_CUBE, { TEXTURE_ASPECT_COLOR, 0, 1, 0, 6 })
            .Execute([&](RDGPassContext context) {       
                
                auto& dispatches = EngineContext::Render()->GetSurfaceCacheManager()->GetDirectLightingDispatches();
                directLightingBuffer[EngineContext::ThreadPool()->ThreadFrameIndex()].SetData(dispatches);

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1); 
                command->BindDescriptorSet(context.descriptors[2], 2);
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
