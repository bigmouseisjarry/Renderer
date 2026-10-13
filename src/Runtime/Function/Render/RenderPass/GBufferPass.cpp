#include "GBufferPass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGBuilder.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/Material.h"
#include "MeshPass.h"
#include "RenderPass.h"
#include <memory>

void GBufferPassProcessor::OnCollectBatch(const DrawBatch& batch)
{
    if(batch.material->RenderPassMask() & PASS_MASK_DEFERRED_PASS) AddBatch(batch);
}

RHIGraphicsPipelineRef GBufferPassProcessor::OnCreatePipeline(const DrawPipelineState& pipelineState)
{
    RHIGraphicsPipelineRef pipeline;

    RHIGraphicsPipelineInfo pipelineInfo    = {};
    pipelineInfo.vertexShader               = pipelineState.vertexShader;
    pipelineInfo.geometryShader             = pipelineState.geometryShader;
    pipelineInfo.fragmentShader             = pipelineState.fragmentShader;
    pipelineInfo.primitiveType              = PRIMITIVE_TYPE_TRIANGLE_LIST;
    pipelineInfo.rasterizerState            = { pipelineState.fillMode, pipelineState.cullMode, DEPTH_CLIP, 0.0f, 0.0f };
    pipelineInfo.depthStencilState          = { pipelineState.depthCompare, pipelineState.depthTest, pipelineState.depthWrite };

    pipelineInfo.rootSignature                  = pass->rootSignature;
    for(uint32_t i = 0; i < MAX_RENDER_TARGETS; i++) pipelineInfo.blendState.renderTargets[i].enable = false;
    pipelineInfo.colorAttachmentFormats[0]      = FORMAT_R8G8B8A8_UNORM;
    pipelineInfo.colorAttachmentFormats[1]      = FORMAT_R8G8B8A8_UNORM;               // 八面体2×12位拼+specF（位模式用UNORM）
    pipelineInfo.colorAttachmentFormats[2]      = FORMAT_R8G8B8A8_UNORM;             // emission RGBM（rgb=e/max(e,1)，a=log2(max)/10，上界2^10）
    pipelineInfo.colorAttachmentFormats[3]      = FORMAT_R32G32_SFLOAT;
    pipelineInfo.colorAttachmentFormats[4]      = FORMAT_R32_UINT;
    pipelineInfo.colorAttachmentFormats[5]      = FORMAT_R8G8B8A8_UNORM;               // 八面体几何法线+AO
    pipelineInfo.colorAttachmentFormats[6]      = FORMAT_R8G8B8A8_UNORM;               // transparent+specF
    pipelineInfo.colorAttachmentFormats[7]      = FORMAT_R8_UNORM;                     // IOR(η-1)/2，薄透明pass写
    pipelineInfo.depthStencilAttachmentFormat   = EngineContext::Render()->GetDepthFormat();

    // if(!pipelineInfo.vertexShader->GetReflectInfo().DefinedSymbol("VERTEX_INPUT"));   // 也可以根据反射信息来检查管线适配性

    pipeline = GraphicsPipelineCache::Get()->Allocate(pipelineInfo).pipeline;     // 按原本提交的管线状态
    if(pipeline) return pipeline;                                                       // TODO 给定的着色器能否满足管线需要其实可以在材质绑定着色器的时候检查和设置标志位？
                                                                                        // 这样只用做一次而不是每帧检查
    pipelineInfo.vertexShader = pipelineState.clusterRender ? 
                                    pass->clusterVertexShader.shader : 
                                    pass->vertexShader.shader;                          // 用默认着色器
    pipelineInfo.geometryShader = nullptr;
    pipelineInfo.fragmentShader = pass->fragmentShader.shader;
    pipeline = GraphicsPipelineCache::Get()->Allocate(pipelineInfo).pipeline;
    if(pipeline) return pipeline;

    if(pipelineState.clusterRender) return pass->clusterPipeline;                       // 用默认管线
    else                            return pass->pipeline;                                   
}

void GBufferPass::Init()
{
    meshPassProcessor = std::make_shared<GBufferPassProcessor>(this);
    MeshPass::Init();

    auto backend = EngineContext::RHI();

    vertexShader        = Shader(EngineContext::File()->ShaderPath() + "default/default.vert.spv", SHADER_FREQUENCY_VERTEX);
    clusterVertexShader = Shader(EngineContext::File()->ShaderPath() + "default/default_cluster.vert.spv", SHADER_FREQUENCY_VERTEX);
    fragmentShader      = Shader(EngineContext::File()->ShaderPath() + "default/deferred.frag.spv", SHADER_FREQUENCY_FRAGMENT);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo());                      
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIGraphicsPipelineInfo pipelineInfo = {};
    pipelineInfo.vertexShader       = vertexShader.shader;
    pipelineInfo.fragmentShader     = fragmentShader.shader;
    pipelineInfo.rootSignature      = rootSignature;
    pipelineInfo.primitiveType      = PRIMITIVE_TYPE_TRIANGLE_LIST;
    pipelineInfo.rasterizerState    = { FILL_MODE_SOLID, CULL_MODE_BACK, DEPTH_CLIP, 0.0f, 0.0f };
    for(uint32_t i = 0; i < MAX_RENDER_TARGETS; i++) pipelineInfo.blendState.renderTargets[i].enable = false;
	pipelineInfo.colorAttachmentFormats[0]      = FORMAT_R8G8B8A8_UNORM;                // diffuse、metallic
    pipelineInfo.colorAttachmentFormats[1]      = FORMAT_R8G8B8A8_UNORM;                // 八面体2×12位拼+specF（位模式用UNORM）
	pipelineInfo.colorAttachmentFormats[2]      = FORMAT_R8G8B8A8_UNORM;             // emission RGBM（rgb=e/max(e,1)，a=log2(max)/10，上界2^10）           // emission（A=IOR）
	pipelineInfo.colorAttachmentFormats[3]      = FORMAT_R32G32_SFLOAT;                 // velocity
    pipelineInfo.colorAttachmentFormats[4]      = FORMAT_R32_UINT;                      // objectID
	pipelineInfo.colorAttachmentFormats[5]      = FORMAT_R8G8B8A8_UNORM;                // 八面体几何法线+AO
	pipelineInfo.colorAttachmentFormats[6]      = FORMAT_R8G8B8A8_UNORM;                // F0+roughness
    pipelineInfo.colorAttachmentFormats[7]      = FORMAT_R8_UNORM;                      // IOR(η-1)/2，薄透明pass写
    pipelineInfo.depthStencilState              = { COMPARE_FUNCTION_LESS_EQUAL, true, true };
    pipelineInfo.depthStencilAttachmentFormat   = EngineContext::Render()->GetDepthFormat();
    pipeline                                    = GraphicsPipelineCache::Get()->Allocate(pipelineInfo).pipeline;    // 普通mesh的默认绘制管线

    pipelineInfo.vertexShader                   = clusterVertexShader.shader;
    clusterPipeline                             = GraphicsPipelineCache::Get()->Allocate(pipelineInfo).pipeline;    // cluster的默认绘制管线
}

void GBufferPass::Build(RDGBuilder& builder) 
{
    Extent2D extent = EngineContext::Render()->GetWindowsExtent();

    RDGTextureHandle diffuse = builder.CreateTexture("G-Buffer Diffuse/Metallic")
        .Exetent({extent.width, extent.height, 1})
        .Format(FORMAT_R8G8B8A8_UNORM)
        .ArrayLayers(1)
        .MipLevels(1)
        .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
        .AllowReadWrite()
        .AllowRenderTarget()
        .Finish();

    RDGTextureHandle normal = builder.CreateTexture("G-Buffer Normal/Roughness")
        .Import(EngineContext::RenderResource()->GetNormalTexture(), RESOURCE_STATE_UNDEFINED)
        .Finish();

    RDGTextureHandle emission = builder.CreateTexture("G-Buffer Emission")
        .Exetent({extent.width, extent.height, 1})
        .Format(FORMAT_R8G8B8A8_UNORM)          // RGBM：rgb=e/max(e,1)，a=log2(max)/10
        .ArrayLayers(1)
        .MipLevels(1)
        .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
        .AllowReadWrite()
        .AllowRenderTarget()
        .Finish();

    RDGTextureHandle velocity = builder.CreateTexture("G-Buffer Velocity")
        .Import(EngineContext::RenderResource()->GetVelocityTexture(), RESOURCE_STATE_UNDEFINED)
        .Finish();

    RDGTextureHandle objectID = builder.CreateTexture("G-Buffer Object ID")
        .Import(EngineContext::RenderResource()->GetObjectIDTexture(), RESOURCE_STATE_UNDEFINED)
        .Finish();

    RDGTextureHandle historyObjectID = builder.CreateTexture("History G-Buffer Object ID")
        .Import(EngineContext::RenderResource()->GetPrevObjectIDTexture(), RESOURCE_STATE_UNDEFINED)
        .Finish();

    RDGTextureHandle historyNormal = builder.CreateTexture("History G-Buffer Normal/Roughness")
        .Import(EngineContext::RenderResource()->GetPrevNormalTexture(), RESOURCE_STATE_UNDEFINED)
        .Finish();

    RDGTextureHandle geometricNormal = builder.CreateTexture("G-Buffer Geometric Normal/AO")
        .Exetent({extent.width, extent.height, 1})
        .Format(FORMAT_R8G8B8A8_UNORM)      // 八面体2×12位拼（位模式用UNORM）+AO
        .ArrayLayers(1)
        .MipLevels(1)
        .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
        .AllowReadWrite()
        .AllowRenderTarget()
        .Finish();

    RDGTextureHandle specularF0 = builder.CreateTexture("G-Buffer Specular F0/Roughness")
        .Exetent({extent.width, extent.height, 1})
        .Format(FORMAT_R8G8B8A8_UNORM)
        .ArrayLayers(1)
        .MipLevels(1)
        .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
        .AllowReadWrite()
        .AllowRenderTarget()
        .Finish();

    RDGTextureHandle ior = builder.CreateTexture("G-Buffer IOR")
        .Import(EngineContext::RenderResource()->GetIORTexture(), RESOURCE_STATE_UNDEFINED)
        .Finish();

    RDGTextureHandle depth = builder.GetTexture("Depth");

    if(IsEnabled())
    {
        RDGCopyPassHandle copyPass0 = builder.CreateCopyPass(GetName() + " Copy Object ID")
            .From(objectID)
            .To(historyObjectID)
            .OutputRead(historyObjectID)
            .Finish();

        RDGCopyPassHandle copyPass1 = builder.CreateCopyPass(GetName() + " Copy Normal")
            .From(normal)
            .To(historyNormal)
            .Finish();

        std::string cmdIndex = " [" + std::to_string(MESH_G_BUFFER_PASS) + "][0]";

        RDGRenderPassHandle pass = builder.CreateRenderPass(GetName())
            .Color(0, diffuse, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
            .Color(1, normal, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
            .Color(2, emission, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
            .Color(3, velocity, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
            .Color(4, objectID, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
            .Color(5, geometricNormal, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 136.0f / 255.0f, 1.0f})   //八面体编码后的+z（kk=2048→B=0x88），AO=1（无遮挡）
            .Color(6, specularF0, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, {0.04f, 0.04f, 0.04f, 1.0f})   //F0=0.04电介质基线，roughness=1
            .Color(7, ior, ATTACHMENT_LOAD_OP_CLEAR, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})              //IOR清0=1.0（无折射），薄透明pass二次写
            .DepthStencil(depth, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, 1.0f, 0)
            .ReadIndirect(builder.GetBuffer("Mesh Draw Commands" + cmdIndex))
            .ReadIndirect(builder.GetBuffer("Clusrter Draw Commands" + cmdIndex))
            .Execute([&](RDGPassContext context) {

                Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

                RHICommandListRef command = context.command;  
                command->SetGraphicsPipeline(pipeline);                                          
                command->SetViewport({0, 0}, {windowExtent.width, windowExtent.height});
                command->SetScissor({0, 0}, {windowExtent.width, windowExtent.height}); 
                command->SetDepthBias(0.0f, 0.0f, 0.0f); 
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);   

                // 这里吃掉Commands，所以gl_InstanceIndex = objectID
                meshPassProcessor->Draw(command);                      
            })
            .OutputRead(depth)  // 后续处理需要读深度
            .OutputRead(velocity)
            .OutputRead(objectID)
            .OutputRead(ior)
            .Finish();
    }
}
