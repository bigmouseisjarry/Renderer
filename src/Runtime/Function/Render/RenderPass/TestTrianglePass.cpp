#include "TestTrianglePass.h"
#include "Core/Math/Math.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGBuilder.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"

void TestTrianglePass::Init()
{
    auto backend = EngineContext::RHI();

    vertexShader    = Shader(EngineContext::File()->ShaderPath() + "test/triangle.vert.spv", SHADER_FREQUENCY_VERTEX);
    fragmentShader  = Shader(EngineContext::File()->ShaderPath() + "test/triangle.frag.spv", SHADER_FREQUENCY_FRAGMENT);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    {
        RHIGraphicsPipelineInfo pipelineInfo    = {};
        pipelineInfo.vertexShader               = vertexShader.shader;
        pipelineInfo.fragmentShader             = fragmentShader.shader;
        pipelineInfo.primitiveType              = PRIMITIVE_TYPE_TRIANGLE_LIST;
        pipelineInfo.rasterizerState            = { FILL_MODE_SOLID, CULL_MODE_NONE, DEPTH_CLIP, 0.0f, 0.0f };                    
        pipelineInfo.depthStencilState          = { COMPARE_FUNCTION_ALWAYS, false, false };

        pipelineInfo.rootSignature                  = rootSignature;
        for(uint32_t i = 0; i < 1; i++) pipelineInfo.blendState.renderTargets[i].enable = false;
        pipelineInfo.colorAttachmentFormats[0]      = EngineContext::Render()->GetColorFormat();                                            
        pipelineInfo.depthStencilAttachmentFormat   = EngineContext::Render()->GetDepthFormat();
        graphicsPipeline                            = EngineContext::RHI()->CreateGraphicsPipeline(pipelineInfo);   
    }

    SetEnable(false);
}   

void TestTrianglePass::Build(RDGBuilder& builder) 
{
    if (IsEnabled())
    {
        Extent2D extent = EngineContext::Render()->GetWindowsExtent();

        RDGTextureHandle outColor = builder.GetOrCreateTexture("Final Color")
            .Exetent({extent.width, extent.height, 1})
            .Format(FORMAT_R8G8B8A8_UNORM)
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .AllowRenderTarget()
            .Finish();

        RDGTextureHandle depth = builder.GetOrCreateTexture("Depth")
            .Import(EngineContext::RenderResource()->GetDepthTexture(), RESOURCE_STATE_UNDEFINED)
            .Finish();  

        RDGRenderPassHandle pass0 = builder.CreateRenderPass(GetName())
            .Color(0, outColor, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
            .DepthStencil(depth, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, 1.0f, 0)
            .Execute([&](RDGPassContext context) {

                Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

                RHICommandListRef command = context.command;  
                command->SetGraphicsPipeline(graphicsPipeline);                                          
                command->SetViewport({0, 0}, {windowExtent.width, windowExtent.height});
                command->SetScissor({0, 0}, {windowExtent.width, windowExtent.height}); 
                command->SetDepthBias(0.0f, 0.0f, 0.0f);   

                command->Draw(3, 1);   
            })
            .Finish();
        
    }
}
