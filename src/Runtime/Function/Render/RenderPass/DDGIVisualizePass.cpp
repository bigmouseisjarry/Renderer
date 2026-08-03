#include "DDGIVisualizePass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderPass/RenderPass.h"

void DDGIVisualizePass::Init()
{
    auto backend = EngineContext::RHI();

    vertexShader = Shader(EngineContext::File()->ShaderPath() + "ddgi/ddgi_probe_visualize.vert.spv", SHADER_FREQUENCY_VERTEX);
    fragmentShader = Shader(EngineContext::File()->ShaderPath() + "ddgi/ddgi_probe_visualize.frag.spv", SHADER_FREQUENCY_FRAGMENT);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddPushConstant({128, SHADER_FREQUENCY_GRAPHICS});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);


    RHIGraphicsPipelineInfo pipelineInfo    = {};
    pipelineInfo.vertexShader               = vertexShader.shader;
    pipelineInfo.fragmentShader             = fragmentShader.shader;
    pipelineInfo.primitiveType              = PRIMITIVE_TYPE_TRIANGLE_LIST;
    pipelineInfo.rasterizerState            = { FILL_MODE_SOLID, CULL_MODE_BACK, DEPTH_CLIP, 0.0f, 0.0f };                    
    pipelineInfo.depthStencilState          = { COMPARE_FUNCTION_LESS_EQUAL, true, true };
    pipelineInfo.rootSignature              = rootSignature;
    pipelineInfo.blendState.renderTargets[0].enable = false;
    pipelineInfo.colorAttachmentFormats[0]      = EngineContext::Render()->GetHdrColorFormat();                                            
    pipelineInfo.depthStencilAttachmentFormat   = EngineContext::Render()->GetDepthFormat();
    graphicsPipeline                            = EngineContext::RHI()->CreateGraphicsPipeline(pipelineInfo);   


    ModelProcessSetting processSetting =  {
        .smoothNormal = true,
        .flipUV = false,
        .loadMaterials = false,
        .tangentSpace = true,
        .generateBVH = false,
        .generateCluster = false,
        .generateVirtualMesh = false,
        .cacheCluster = false
    };
    model = std::make_shared<Model>("Asset/BuildIn/Model/Basic/sphere.obj", processSetting);
}   

void DDGIVisualizePass::Build(RDGBuilder& builder) 
{
    if( IsEnabled() &&
        !EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) &&
        !EngineContext::Render()->IsPassEnabled(RAY_TRACING_BASE_PASS) && 
        !EngineContext::Render()->IsPassEnabled(PATH_TRACING_PASS))
    {
        RDGTextureHandle outColor = builder.GetTexture("Mesh Pass Out Color");

        RDGTextureHandle depth = builder.GetTexture("Depth");

        volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()] = EngineContext::Render()->GetLightManager()->GetVolumeLights();

        for (uint32_t i = 0; i < volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()].size(); i++)
		{
            auto& volumeLight = volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()][i];

			if (volumeLight->Enable() &&
				volumeLight->Visualize())
			{
                std::string index = " [" + std::to_string(volumeLight->GetVolumeLightID()) + "]";

                RDGRenderPassHandle pass = builder.CreateRenderPass(GetName() + index)
                    .PassIndex(i)
                    .Color(0, outColor, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
                    .DepthStencil(depth, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, 1.0f, 0)
                    .Execute([&](RDGPassContext context) {

                        auto& volumeLight = volumeLights[EngineContext::ThreadPool()->ThreadFrameIndex()][context.passIndex[0]];

                        DDGIVisualizeSetting setting = {};
                        setting.volumeLightID   = volumeLight->GetVolumeLightID();
                        setting.visualizeMode   = volumeLight->GetVisulaizeMode();
                        setting.probeScale      = volumeLight->GetVisulaizeProbeScale();
                        setting.vertexID        = model->GetVertexBuffer(0)->vertexID;

                        int instanceCount = volumeLight->GetProbeCounts().x() * 
                                            volumeLight->GetProbeCounts().y() *
                                            volumeLight->GetProbeCounts().z();

                        Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

                        RHICommandListRef command = context.command;    
                        command->SetGraphicsPipeline(graphicsPipeline);                                        
                        command->SetViewport({0, 0}, {windowExtent.width, windowExtent.height});
                        command->SetScissor({0, 0}, {windowExtent.width, windowExtent.height}); 
                        command->SetDepthBias(0.0f, 0.0f, 0.0f);          
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);   
                        command->PushConstants(&setting, sizeof(DDGIVisualizeSetting), SHADER_FREQUENCY_GRAPHICS);

                        command->BindIndexBuffer(model->GetIndexBuffer(0)->buffer);
                        command->DrawIndexed(model->GetIndexBuffer(0)->IndexNum(), instanceCount);                    
                    })
                    .Finish();
            }
        }      
    }
}
