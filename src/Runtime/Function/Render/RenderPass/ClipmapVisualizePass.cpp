#include "ClipmapVisualizePass.h"
#include "Core/Math/Math.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderPass/RenderPass.h"
#include "ClipmapPass.h"
#include <cmath>
#include <cstdint>
#include <memory>

void ClipmapVisualizePass::Init()
{
    auto backend = EngineContext::RHI();

    vertexShader    = Shader(EngineContext::File()->ShaderPath() + "clipmap/clipmap_visualize.vert.spv", SHADER_FREQUENCY_VERTEX);
    geometryShader  = Shader(EngineContext::File()->ShaderPath() + "clipmap/clipmap_visualize.geom.spv", SHADER_FREQUENCY_GEOMETRY);
    fragmentShader  = Shader(EngineContext::File()->ShaderPath() + "clipmap/clipmap_visualize.frag.spv", SHADER_FREQUENCY_FRAGMENT);

    rayGenShader     = Shader(EngineContext::File()->ShaderPath() + "clipmap/clipmap_visualize.rgen.spv", SHADER_FREQUENCY_RAY_GEN);
    rayMissShader    = Shader(EngineContext::File()->ShaderPath() + "clipmap/clipmap_visualize.rmiss.spv", SHADER_FREQUENCY_RAY_MISS);
    closestHitShader = Shader(EngineContext::File()->ShaderPath() + "clipmap/clipmap_visualize.rchit.spv", SHADER_FREQUENCY_CLOSEST_HIT);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry({1, 0, 1, SHADER_FREQUENCY_GRAPHICS | SHADER_FREQUENCY_RAY_TRACING, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 1, 1, SHADER_FREQUENCY_GRAPHICS | SHADER_FREQUENCY_RAY_TRACING, RESOURCE_TYPE_RW_BUFFER})
                     .AddEntry({1, 2, 1, SHADER_FREQUENCY_RAY_TRACING, RESOURCE_TYPE_RW_TEXTURE})
                     .AddPushConstant({128, SHADER_FREQUENCY_GRAPHICS});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    {
        RHIGraphicsPipelineInfo pipelineInfo    = {};
        pipelineInfo.vertexShader               = vertexShader.shader;
        pipelineInfo.geometryShader             = geometryShader.shader;
        pipelineInfo.fragmentShader             = fragmentShader.shader;
        pipelineInfo.primitiveType              = PRIMITIVE_TYPE_POINT_LIST;
        pipelineInfo.rasterizerState            = { FILL_MODE_SOLID, CULL_MODE_NONE, DEPTH_CLIP, 0.0f, 0.0f };                    
        pipelineInfo.depthStencilState          = { COMPARE_FUNCTION_LESS_EQUAL, true, true };
        pipelineInfo.rootSignature              = rootSignature;
        pipelineInfo.blendState.renderTargets[0].enable = false;
        // pipelineInfo.rasterizerState            = { FILL_MODE_SOLID, CULL_MODE_NONE, DEPTH_CLIP, 0.0f, 0.0f };                    
        // pipelineInfo.depthStencilState          = { COMPARE_FUNCTION_LESS_EQUAL, false, false };
        // pipelineInfo.rootSignature              = rootSignature;
        // pipelineInfo.blendState.renderTargets[0] = { BLEND_OP_ADD, BLEND_FACTOR_SRC_ALPHA, BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        //                                              BLEND_OP_ADD, BLEND_FACTOR_SRC_ALPHA, BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        //                                              COLOR_MASK_RGBA, true};
        pipelineInfo.colorAttachmentFormats[0]      = EngineContext::Render()->GetHdrColorFormat();                                            
        pipelineInfo.depthStencilAttachmentFormat   = EngineContext::Render()->GetDepthFormat();
        graphicsPipeline                            = EngineContext::RHI()->CreateGraphicsPipeline(pipelineInfo);   
    }

    {
        RHIShaderBindingTableInfo sbtInfo = {};
        sbtInfo.AddRayGenGroup(rayGenShader.shader);
        sbtInfo.AddHitGroup(closestHitShader.shader, nullptr, nullptr);
        sbtInfo.AddMissGroup(rayMissShader.shader);
        RHIShaderBindingTableRef sbt = EngineContext::RHI()->CreateShaderBindingTable(sbtInfo);

        RHIRayTracingPipelineInfo pipelineInfo  = {};
        pipelineInfo.rootSignature              = rootSignature;
        pipelineInfo.shaderBindingTable         = sbt;
        rayTracingPipeline   = backend->CreateRayTracingPipeline(pipelineInfo);
    }
}   

void ClipmapVisualizePass::Build(RDGBuilder& builder) 
{
    if( IsEnabled() &&
        !EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) &&
        !EngineContext::Render()->IsPassEnabled(RAY_TRACING_BASE_PASS) && 
        !EngineContext::Render()->IsPassEnabled(PATH_TRACING_PASS))
    {
        RDGTextureHandle outColor = builder.GetTexture("Mesh Pass Out Color");

        RDGTextureHandle depth = builder.GetTexture("Depth");

        RDGBufferHandle clipmapBuf = builder.GetBuffer("VXGI Clipmap Buffer");
        RDGTextureHandle clipmapTex = builder.GetTexture("VXGI Clipmap Texture");

        clipmap = std::dynamic_pointer_cast<ClipmapPass>(EngineContext::Render()->GetPasses()[CLIPMAP_PASS])->GetClipmap();

        if(mode == 0)   // 光栅化体素
        {
            for(int i = setting.minMip; i < std::min(clipmap->LevelCount(), setting.maxMip); i++)
            {
                std::string index = " [" + std::to_string(i) + "]";

                RDGRenderPassHandle pass = builder.CreateRenderPass(GetName() + index)
                    .PassIndex(i)
                    .RootSignature(rootSignature)
                    .Color(0, outColor, (i == setting.minMip) ? ATTACHMENT_LOAD_OP_CLEAR : ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
                    .DepthStencil(depth, (i == setting.minMip) ? ATTACHMENT_LOAD_OP_CLEAR : ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, 1.0f, 0)
                    .ReadWrite(1, 0, 0, clipmapTex, VIEW_TYPE_3D)
                    .ReadWrite(1, 1, 0, clipmapBuf)
                    .Execute([&](RDGPassContext context) {
                        
                        ClipmapVisualizeSetting newSetting = setting;
                        newSetting.mipLevel = context.passIndex[0];
                        int vertexCount = std::pow(clipmap->GetLevel(newSetting.mipLevel).extent, 3);

                        Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

                        RHICommandListRef command = context.command;    
                        command->SetGraphicsPipeline(graphicsPipeline);                                        
                        command->SetViewport({0, 0}, {windowExtent.width, windowExtent.height});
                        command->SetScissor({0, 0}, {windowExtent.width, windowExtent.height}); 
                        command->SetDepthBias(0.0f, 0.0f, 0.0f);          
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);   
                        command->BindDescriptorSet(context.descriptors[1], 1);
                        command->PushConstants(&newSetting, sizeof(ClipmapVisualizeSetting), SHADER_FREQUENCY_GRAPHICS);

                        command->Draw(vertexCount, 1, 0, 0);                    
                    })
                    .Finish();
            }
        }
        else {      // 光追
            
            RDGRayTracingPassHandle pass = builder.CreateRayTracingPass(GetName() + " Ray tracing")
                .RootSignature(rootSignature)
                .ReadWrite(1, 0, 0, clipmapTex, VIEW_TYPE_3D)
                .ReadWrite(1, 1, 0, clipmapBuf)
                .ReadWrite(1, 2, 0, outColor)
                .Execute([&](RDGPassContext context) {       

                    RHICommandListRef command = context.command; 
                    command->SetRayTracingPipeline(rayTracingPipeline);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);  
                    command->BindDescriptorSet(context.descriptors[1], 1);
                    command->TraceRays(  EngineContext::Render()->GetWindowsExtent().width, 
                                        EngineContext::Render()->GetWindowsExtent().height, 
                                        1);
                })
                .Finish();
        }
    }
}
