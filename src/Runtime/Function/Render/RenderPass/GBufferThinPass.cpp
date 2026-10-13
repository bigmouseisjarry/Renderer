#include "GBufferThinPass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGBuilder.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/Material.h"
#include "MeshPass.h"
#include "RenderPass.h"
#include <memory>

void GBufferThinPassProcessor::OnCollectBatch(const DrawBatch& batch)
{
    if(batch.material->RenderPassMask() & PASS_MASK_DEFERRED_THIN_PASS) AddBatch(batch);
}

RHIGraphicsPipelineRef GBufferThinPassProcessor::OnCreatePipeline(const DrawPipelineState& pipelineState)
{
    RHIGraphicsPipelineRef pipeline;

    RHIGraphicsPipelineInfo pipelineInfo    = {};
    pipelineInfo.vertexShader               = pipelineState.vertexShader;
    pipelineInfo.geometryShader             = pipelineState.geometryShader;
    pipelineInfo.fragmentShader             = pipelineState.fragmentShader;
    pipelineInfo.primitiveType              = PRIMITIVE_TYPE_TRIANGLE_LIST;
    pipelineInfo.rasterizerState            = { pipelineState.fillMode, pipelineState.cullMode, DEPTH_CLIP, 0.0f, 0.0f };
    // 与不透明G-Buffer唯一的管线差异：不写深度（薄层不污染HiZ/SSR的不透明语义）。
    // 材质透传的depthWrite位不采纳——合批key仍按材质位分裂，只会过分裂不会错批。
    pipelineInfo.depthStencilState          = { COMPARE_FUNCTION_LESS_EQUAL, true, false };

    pipelineInfo.rootSignature                  = pass->rootSignature;
    for(uint32_t i = 0; i < MAX_RENDER_TARGETS; i++) pipelineInfo.blendState.renderTargets[i].enable = false;
    pipelineInfo.colorAttachmentFormats[0]      = FORMAT_R8G8B8A8_UNORM;               // diffuse预乘+transparent
    pipelineInfo.colorAttachmentFormats[1]      = FORMAT_R8G8B8A8_UNORM;               // 八面体2×12位拼+specF
    pipelineInfo.colorAttachmentFormats[2]      = FORMAT_R8G8B8A8_UNORM;               // emission RGBM
    pipelineInfo.colorAttachmentFormats[3]      = FORMAT_R32G32_SFLOAT;                // velocity
    pipelineInfo.colorAttachmentFormats[4]      = FORMAT_R32_UINT;                     // objectID
    pipelineInfo.colorAttachmentFormats[5]      = FORMAT_R8G8B8A8_UNORM;               // 八面体几何法线+AO
    pipelineInfo.colorAttachmentFormats[6]      = FORMAT_R8G8B8A8_UNORM;               // F0+roughness
    pipelineInfo.colorAttachmentFormats[7]      = FORMAT_R8_UNORM;                     // IOR(η-1)/2
    pipelineInfo.depthStencilAttachmentFormat   = EngineContext::Render()->GetDepthFormat();

    pipeline = GraphicsPipelineCache::Get()->Allocate(pipelineInfo).pipeline;     // 按原本提交的管线状态
    if(pipeline) return pipeline;

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

void GBufferThinPass::Init()
{
    meshPassProcessor = std::make_shared<GBufferThinPassProcessor>(this);
    MeshPass::Init();

    auto backend = EngineContext::RHI();

    vertexShader        = Shader(EngineContext::File()->ShaderPath() + "default/default.vert.spv", SHADER_FREQUENCY_VERTEX);
    clusterVertexShader = Shader(EngineContext::File()->ShaderPath() + "default/default_cluster.vert.spv", SHADER_FREQUENCY_VERTEX);
    fragmentShader      = Shader(EngineContext::File()->ShaderPath() + "default/deferred.frag.spv", SHADER_FREQUENCY_FRAGMENT);   // 与不透明G-Buffer共用，零变体

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
    pipelineInfo.colorAttachmentFormats[0]      = FORMAT_R8G8B8A8_UNORM;
    pipelineInfo.colorAttachmentFormats[1]      = FORMAT_R8G8B8A8_UNORM;
    pipelineInfo.colorAttachmentFormats[2]      = FORMAT_R8G8B8A8_UNORM;
    pipelineInfo.colorAttachmentFormats[3]      = FORMAT_R32G32_SFLOAT;
    pipelineInfo.colorAttachmentFormats[4]      = FORMAT_R32_UINT;
    pipelineInfo.colorAttachmentFormats[5]      = FORMAT_R8G8B8A8_UNORM;
    pipelineInfo.colorAttachmentFormats[6]      = FORMAT_R8G8B8A8_UNORM;
    pipelineInfo.colorAttachmentFormats[7]      = FORMAT_R8_UNORM;
    pipelineInfo.depthStencilState              = { COMPARE_FUNCTION_LESS_EQUAL, true, false };    // 不写深度
    pipelineInfo.depthStencilAttachmentFormat   = EngineContext::Render()->GetDepthFormat();
    pipeline                                    = GraphicsPipelineCache::Get()->Allocate(pipelineInfo).pipeline;    // 普通mesh的默认绘制管线

    pipelineInfo.vertexShader                   = clusterVertexShader.shader;
    clusterPipeline                             = GraphicsPipelineCache::Get()->Allocate(pipelineInfo).pipeline;    // cluster的默认绘制管线
}

void GBufferThinPass::Build(RDGBuilder& builder)
{
    if(IsEnabled())
    {
        // 全部按名取不透明G-Buffer已声明的纹理（RDG名字注册表保证拓扑序：本pass排在其后）
        RDGTextureHandle diffuse        = builder.GetTexture("G-Buffer Diffuse/Metallic");
        RDGTextureHandle normal         = builder.GetTexture("G-Buffer Normal/Roughness");
        RDGTextureHandle emission       = builder.GetTexture("G-Buffer Emission");
        RDGTextureHandle velocity       = builder.GetTexture("G-Buffer Velocity");
        RDGTextureHandle objectID       = builder.GetTexture("G-Buffer Object ID");
        RDGTextureHandle geometricNormal = builder.GetTexture("G-Buffer Geometric Normal/AO");
        RDGTextureHandle specularF0     = builder.GetTexture("G-Buffer Specular F0/Roughness");
        RDGTextureHandle ior            = builder.CreateTexture("G-Buffer IOR")
                                            .Import(EngineContext::RenderResource()->GetIORTexture(), RESOURCE_STATE_UNDEFINED)
                                            .Finish();
        RDGTextureHandle depth          = builder.GetTexture("Depth");

        // 间接绘制命令buffer的虚拟依赖边（见GPUCullingPass的Import命名）
        std::string cmdIndex = " [" + std::to_string(MESH_G_BUFFER_THIN_PASS) + "][0]";

        RDGRenderPassHandle pass = builder.CreateRenderPass(GetName())
            .Color(0, diffuse, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})       // 叠写不透明结果
            .Color(1, normal, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
            .Color(2, emission, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
            .Color(3, velocity, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
            .Color(4, objectID, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
            .Color(5, geometricNormal, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
            .Color(6, specularF0, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, {0.04f, 0.04f, 0.04f, 1.0f})
            .Color(7, ior, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})           // 不透明pass已CLEAR，这里LOAD保住并写真实值
            .DepthStencil(depth, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, 1.0f, 0)                       // depthWrite=false由管线保证
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

                meshPassProcessor->Draw(command);
            })
            .OutputRead(depth)
            .OutputRead(velocity)
            .OutputRead(objectID)
            .OutputRead(ior)
            .Finish();
    }
}
