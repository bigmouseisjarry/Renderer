#include "DepthPass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"

void DepthPassProcessor::OnCollectBatch(const DrawBatch& batch)
{
    if(batch.material->UseForDepthPass()) AddBatch(batch);
}

RHIGraphicsPipelineRef DepthPassProcessor::OnCreatePipeline(const DrawPipelineState& pipelineState)
{
    if(pipelineState.clusterRender) return pass->clusterPipeline;                       // 用默认管线
    else                            return pass->pipeline;                                                      
}

void DepthPass::Init()
{
    meshPassProcessor = std::make_shared<DepthPassProcessor>(this);
    MeshPass::Init();

    auto backend = EngineContext::RHI();

    vertexShader        = Shader(EngineContext::File()->ShaderPath() + "depth/depth.vert.spv", SHADER_FREQUENCY_VERTEX);
    clusterVertexShader = Shader(EngineContext::File()->ShaderPath() + "depth/depth_cluster.vert.spv", SHADER_FREQUENCY_VERTEX);
    fragmentShader      = Shader(EngineContext::File()->ShaderPath() + "depth/depth.frag.spv", SHADER_FREQUENCY_FRAGMENT);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo());     
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIGraphicsPipelineInfo pipelineInfo = {};
    pipelineInfo.vertexShader       = vertexShader.shader;
    pipelineInfo.fragmentShader     = fragmentShader.shader;
    pipelineInfo.rootSignature      = rootSignature;
    pipelineInfo.primitiveType      = PRIMITIVE_TYPE_TRIANGLE_LIST;
    pipelineInfo.rasterizerState    = { FILL_MODE_SOLID, CULL_MODE_BACK, DEPTH_CLIP, 0.0f, 0.0f };                    
    pipelineInfo.depthStencilState              = { COMPARE_FUNCTION_LESS_EQUAL, true, true };
    pipelineInfo.depthStencilAttachmentFormat   = EngineContext::Render()->GetDepthFormat();
    pipeline                                    = GraphicsPipelineCache::Get()->Allocate(pipelineInfo).pipeline;    // 普通mesh的默认绘制管线

    pipelineInfo.vertexShader                   = clusterVertexShader.shader;
    clusterPipeline                             = GraphicsPipelineCache::Get()->Allocate(pipelineInfo).pipeline;    // cluster的默认绘制管线
}   

void DepthPass::Build(RDGBuilder& builder) 
{
    RDGTextureHandle depth = builder.CreateTexture("Depth")
        .Import(EngineContext::RenderResource()->GetDepthTexture())
        .Finish();

    RDGTextureHandle prevDepth = builder.CreateTexture("Previous Depth")
        .Import(EngineContext::RenderResource()->GetPrevDepthTexture())
        .Finish();

    RDGCopyPassHandle copyPass = builder.CreateCopyPass(GetName() + " Copy")
        .From(depth)
        .To(prevDepth)
        .OutputRead(prevDepth)
        .Finish();

    std::string cmdIndex = " [" + std::to_string(MESH_DEPTH_PASS) + "][0]";

    RDGRenderPassHandle pass = builder.CreateRenderPass(GetName())
        .DepthStencil(depth, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, 1.0f, 0)
        .ReadIndirect(builder.GetBuffer("Mesh Draw Commands" + cmdIndex))
        .ReadIndirect(builder.GetBuffer("Clusrter Draw Commands" + cmdIndex))
        .RootSignature(rootSignature)     
        .Read(2, SET2_BINDING_CLUSTER_DRAW_INFOS_READ, 0, builder.GetBuffer("Cluster Draw Infos" + cmdIndex), 0, 0, RESOURCE_TYPE_BUFFER)
        .Execute([&](RDGPassContext context) {

            Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

            RHICommandListRef command = context.command;
            command->SetGraphicsPipeline(pipeline);     // 这一行似乎是多余的？
            command->SetViewport({0, 0}, {windowExtent.width, windowExtent.height});
            command->SetScissor({0, 0}, {windowExtent.width, windowExtent.height});
            command->SetDepthBias(0.0f,
                                    0.0f,
                                    0.0f);
            command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
            command->BindDescriptorSet(context.descriptors[2], 2);

            meshPassProcessor->Draw(command);
        })
        .Finish();
}
