#include "DirectionalShadowPass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"

void DirectionalShadowPassProcessor::OnCollectBatch(const DrawBatch& batch)
{
    if(batch.material->CastShadow()) AddBatch(batch);
}

RHIGraphicsPipelineRef DirectionalShadowPassProcessor::OnCreatePipeline(const DrawPipelineState& pipelineState)
{
    if(pipelineState.clusterRender) return pass->clusterPipeline;                       // 用默认管线
    else                            return pass->pipeline;                                                      
}

void DirectionalShadowPass::Init()
{
    meshPassProcessor = std::make_shared<DirectionalShadowPassProcessor>(this);
    meshPassProcessor->Init(DIRECTIONAL_SHADOW_CASCADE_LEVEL);

    auto backend = EngineContext::RHI();

    vertexShader        = Shader(EngineContext::File()->ShaderPath() + "shadow/directional_shadow.vert.spv", SHADER_FREQUENCY_VERTEX);
    clusterVertexShader = Shader(EngineContext::File()->ShaderPath() + "shadow/directional_shadow_cluster.vert.spv", SHADER_FREQUENCY_VERTEX);
    fragmentShader      = Shader(EngineContext::File()->ShaderPath() + "shadow/directional_shadow.frag.spv", SHADER_FREQUENCY_FRAGMENT);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddPushConstant({128, SHADER_FREQUENCY_GRAPHICS});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIGraphicsPipelineInfo pipelineInfo = {};
    pipelineInfo.vertexShader       = vertexShader.shader;
    pipelineInfo.fragmentShader     = fragmentShader.shader; // 暂时不需要
    pipelineInfo.rootSignature      = rootSignature;
    pipelineInfo.primitiveType      = PRIMITIVE_TYPE_TRIANGLE_LIST;
    pipelineInfo.rasterizerState    = { FILL_MODE_SOLID, CULL_MODE_BACK, DEPTH_CLIP, 0.0f, 0.0f };                   
    pipelineInfo.depthStencilState              = { COMPARE_FUNCTION_LESS_EQUAL, true, true };
    pipelineInfo.depthStencilAttachmentFormat   = EngineContext::Render()->GetDepthFormat();
    pipeline                                    = GraphicsPipelineCache::Get()->Allocate(pipelineInfo).pipeline;    // 普通mesh的默认绘制管线

    pipelineInfo.vertexShader                   = clusterVertexShader.shader;
    clusterPipeline                             = GraphicsPipelineCache::Get()->Allocate(pipelineInfo).pipeline;    // cluster的默认绘制管线
}   

void DirectionalShadowPass::Build(RDGBuilder& builder) 
{
    auto directionalLightComponent = EngineContext::Render()->GetLightManager()->GetDirectionalLight();
    if( directionalLightComponent && 
        directionalLightComponent->Enable() && 
        directionalLightComponent->CastShadow() &&
        IsEnabled() &&
        !EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) &&
        // !EngineContext::Render()->IsPassEnabled(RAY_TRACING_BASE_PASS) &&    //Surface cache有使用
        !EngineContext::Render()->IsPassEnabled(PATH_TRACING_PASS))
    {
        for(uint32_t i = 0; i < DIRECTIONAL_SHADOW_CASCADE_LEVEL; i++)
        {
            if(directionalLightComponent->ShouldUpdate(i))
            {
                std::string index = " [" + std::to_string(i) + "]";

                RDGTextureHandle depth = builder.CreateTexture("Directional Depth" + index)
                    .Import(EngineContext::RenderResource()->GetDirectionalShadowTexture(i), RESOURCE_STATE_UNDEFINED)
                    .Finish();

                RDGRenderPassHandle pass = builder.CreateRenderPass(GetName() + index)
                    .PassIndex(i)
                    .DepthStencil(depth, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, 1.0f, 0)
                    .Execute([&](RDGPassContext context) {

                        Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();
                        auto directionalLight = EngineContext::Render()->GetLightManager()->GetDirectionalLight();
                        uint32_t index = context.passIndex[0];

                        RHICommandListRef command = context.command;                                            
                        command->SetGraphicsPipeline(pipeline);
                        command->SetViewport({0, 0}, {DIRECTIONAL_SHADOW_SIZE, DIRECTIONAL_SHADOW_SIZE});
                        command->SetScissor({0, 0}, {DIRECTIONAL_SHADOW_SIZE, DIRECTIONAL_SHADOW_SIZE}); 
                        command->SetDepthBias(directionalLight->GetConstantBias(), 
                                                 directionalLight->GetSlopeBias(), 
                                                 0.0f);
                        command->PushConstants(&index, sizeof(uint32_t), SHADER_FREQUENCY_GRAPHICS);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);   

                        meshPassProcessor->Draw(command, index);                      
                    })
                    .OutputRead(depth)  // 输出，作为后续阴影绘制的SRV
                    .Finish();
            }
        }
    }
}
