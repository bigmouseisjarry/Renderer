#include "GizmoPass.h"
#include "Core/Math/Math.h"
#include "Function/Global/Definations.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderPass/RenderPass.h"
#include "ClipmapPass.h"
#include <cmath>
#include <cstdint>
#include <memory>

void GizmoPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader       = Shader(EngineContext::File()->ShaderPath() + "gizmo/init_gizmo.comp.spv", SHADER_FREQUENCY_COMPUTE);
    vertexShader[0]     = Shader(EngineContext::File()->ShaderPath() + "gizmo/cube.vert.spv", SHADER_FREQUENCY_VERTEX);
    vertexShader[1]     = Shader(EngineContext::File()->ShaderPath() + "gizmo/sphere.vert.spv", SHADER_FREQUENCY_VERTEX);
    vertexShader[2]     = Shader(EngineContext::File()->ShaderPath() + "gizmo/point.vert.spv", SHADER_FREQUENCY_VERTEX);
    geometryShader[0]   = Shader(EngineContext::File()->ShaderPath() + "gizmo/line.geom.spv", SHADER_FREQUENCY_GEOMETRY);
    geometryShader[1]   = Shader(EngineContext::File()->ShaderPath() + "gizmo/billboard.geom.spv", SHADER_FREQUENCY_GEOMETRY);
    fragmentShader[0]   = Shader(EngineContext::File()->ShaderPath() + "gizmo/wire.frag.spv", SHADER_FREQUENCY_FRAGMENT);
    fragmentShader[1]   = Shader(EngineContext::File()->ShaderPath() + "gizmo/texture.frag.spv", SHADER_FREQUENCY_FRAGMENT);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddPushConstant({128, SHADER_FREQUENCY_GRAPHICS});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    {
        RHIComputePipelineInfo pipelineInfo     = {};
        pipelineInfo.rootSignature              = rootSignature;
        pipelineInfo.computeShader              = computeShader.shader;
        computePipeline   = backend->CreateComputePipeline(pipelineInfo);
    }


    RHIGraphicsPipelineInfo pipelineInfo    = {};
    pipelineInfo.fragmentShader             = fragmentShader[0].shader;
    pipelineInfo.primitiveType              = PRIMITIVE_TYPE_TRIANGLE_LIST;
    pipelineInfo.rasterizerState            = { FILL_MODE_WIREFRAME, CULL_MODE_NONE, DEPTH_CLIP, 0.0f, 0.0f };                    
    pipelineInfo.depthStencilState          = { COMPARE_FUNCTION_LESS_EQUAL, true, true };
    pipelineInfo.rootSignature              = rootSignature;
    pipelineInfo.blendState.renderTargets[0].enable = false;
    // pipelineInfo.blendState.renderTargets[0] = { BLEND_OP_ADD, BLEND_FACTOR_SRC_ALPHA, BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    //                                             BLEND_OP_ADD, BLEND_FACTOR_SRC_ALPHA, BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    //                                             COLOR_MASK_RGBA, true};
    pipelineInfo.colorAttachmentFormats[0]      = EngineContext::Render()->GetColorFormat();                                            
    pipelineInfo.depthStencilAttachmentFormat   = EngineContext::Render()->GetDepthFormat();
    pipelineInfo.vertexInputState.vertexElements.push_back({
        .streamIndex = 0,
        .attributeIndex = 0, 
        .format = FORMAT_R32G32B32_SFLOAT,
        .offset = 0,
        .stride = 3 * sizeof(float),
        .useInstanceIndex = false,
    });

    // box
    pipelineInfo.vertexShader               = vertexShader[0].shader;
    graphicsPipeline[0] = EngineContext::RHI()->CreateGraphicsPipeline(pipelineInfo);

    // sphere
    pipelineInfo.vertexShader               = vertexShader[1].shader;
    graphicsPipeline[1] = EngineContext::RHI()->CreateGraphicsPipeline(pipelineInfo);   

    // line
    pipelineInfo.vertexShader               = vertexShader[2].shader;
    pipelineInfo.geometryShader             = geometryShader[0].shader;
    pipelineInfo.primitiveType              = PRIMITIVE_TYPE_POINT_LIST;
    pipelineInfo.vertexInputState.vertexElements.clear();
    graphicsPipeline[2] = EngineContext::RHI()->CreateGraphicsPipeline(pipelineInfo);  

    // billboard
    pipelineInfo.vertexShader               = vertexShader[2].shader;
    pipelineInfo.geometryShader             = geometryShader[1].shader;
    pipelineInfo.fragmentShader             = fragmentShader[1].shader;
    pipelineInfo.primitiveType              = PRIMITIVE_TYPE_POINT_LIST;
    pipelineInfo.vertexInputState.vertexElements.clear();
    pipelineInfo.rasterizerState            = { FILL_MODE_SOLID, CULL_MODE_NONE, DEPTH_CLIP, 0.0f, 0.0f };  
    graphicsPipeline[3] = EngineContext::RHI()->CreateGraphicsPipeline(pipelineInfo);  

    ModelProcessSetting processSetting =  {
        .smoothNormal = false,
        .flipUV = false,
        .loadMaterials = false,
        .tangentSpace = false,
        .generateBVH = false,
        .generateCluster = false,
        .generateVirtualMesh = false,
        .cacheCluster = false
    };

    cubeWire = std::make_shared<Model>("Asset/BuildIn/Model/Basic/cube_wire.obj", processSetting);
    command[0] = {
        .indexCount = cubeWire->GetIndexBuffer(0)->IndexNum(),
        .instanceCount = 0,
        .firstIndex = 0,
        .vertexOffset = 0,
        .firstInstance = 0
    };

    sphereWire = std::make_shared<Model>("Asset/BuildIn/Model/Basic/sphere_wire.obj", processSetting);
    command[1] = {
        .indexCount = sphereWire->GetIndexBuffer(0)->IndexNum(),
        .instanceCount = 0,
        .firstIndex = 0,
        .vertexOffset = 0,
        .firstInstance = 0
    };

    command[2] = {
        .indexCount = 1,
        .instanceCount = 0,
        .firstIndex = 0,
        .vertexOffset = 0,
        .firstInstance = 0
    };

    command[3] = {
        .indexCount = 1,
        .instanceCount = 0,
        .firstIndex = 0,
        .vertexOffset = 0,
        .firstInstance = 0
    };
}   

void GizmoPass::Build(RDGBuilder& builder) 
{
    EngineContext::RenderResource()->SetGizmoDataCommand(&command[0], 4);  // 每帧执行前先把buffer清空

    if(IsEnabled())
    {
        RDGTextureHandle outColor = builder.GetTexture("Final Color");
        RDGTextureHandle depth = builder.GetTexture("Depth");

        RDGBufferHandle dataBuffer = builder.CreateBuffer("Gizmo Data")
            .Import(EngineContext::RenderResource()->GetGizmoDataBuffer(), RESOURCE_STATE_UNDEFINED)
            .Finish();

        RDGComputePassHandle pass0 = builder.CreateComputePass(GetName() + " Init")
            //.RootSignature(rootSignature)
            //.ReadWrite(1, 0, 0, dataBuffer)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->Dispatch(  std::max(MAX_PER_FRAME_CLUSTER_SIZE, MAX_POINT_LIGHT_COUNT) / 256, 
                                    1, 
                                    1);
            })
            .OutputIndirectDraw(dataBuffer)
            .Finish();

        RDGRenderPassHandle pass1 = builder.CreateRenderPass(GetName())
            .Color(0, outColor, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, {0.0f, 0.0f, 0.0f, 0.0f})
            .DepthStencil(depth, ATTACHMENT_LOAD_OP_LOAD, ATTACHMENT_STORE_OP_STORE, 1.0f, 0)  
            .Execute([&](RDGPassContext context) {
                
                Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

                RHICommandListRef command = context.command;     
                command->SetGraphicsPipeline(graphicsPipeline[0]);                                        
                command->SetViewport({0, 0}, {windowExtent.width, windowExtent.height});
                command->SetScissor({0, 0}, {windowExtent.width, windowExtent.height}); 
                command->SetLineWidth(1.0f);
                command->SetDepthBias(0.0f, 0.0f, 0.0f);          
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);    

                command->BindVertexBuffer(cubeWire->GetVertexBuffer(0)->positionBuffer, 0, 0);
                command->BindIndexBuffer(cubeWire->GetIndexBuffer(0)->buffer, 0);
                command->DrawIndexedIndirect(EngineContext::RenderResource()->GetGizmoDataBuffer(), 0, 1);

                command->SetGraphicsPipeline(graphicsPipeline[1]);   
                command->BindVertexBuffer(sphereWire->GetVertexBuffer(0)->positionBuffer, 0, 0);
                command->BindIndexBuffer(sphereWire->GetIndexBuffer(0)->buffer, 0);
                command->DrawIndexedIndirect(EngineContext::RenderResource()->GetGizmoDataBuffer(), sizeof(RHIIndexedIndirectCommand), 1);

                command->SetGraphicsPipeline(graphicsPipeline[2]);   
                command->DrawIndexedIndirect(EngineContext::RenderResource()->GetGizmoDataBuffer(), 2 * sizeof(RHIIndexedIndirectCommand), 1);

                command->SetGraphicsPipeline(graphicsPipeline[3]);   
                command->DrawIndexedIndirect(EngineContext::RenderResource()->GetGizmoDataBuffer(), 3 * sizeof(RHIIndexedIndirectCommand), 1);
            })
            .Finish();
    }
}
