#include "ClusterLightingPass.h"
#include "Function/Global/Definations.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderPass/RenderPass.h"

void ClusterLightingPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader = Shader(EngineContext::File()->ShaderPath() + "culling/cluster_lighting.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo());      
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;
    pipelineInfo.computeShader              = computeShader.shader;
    computePipeline   = backend->CreateComputePipeline(pipelineInfo);
}   

void ClusterLightingPass::Build(RDGBuilder& builder) 
{
    if( IsEnabled() && 
        !EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) &&
        !EngineContext::Render()->IsPassEnabled(RAY_TRACING_BASE_PASS) && 
        !EngineContext::Render()->IsPassEnabled(PATH_TRACING_PASS))
    {
        // 稳态均=SRV：本pass的OutputReadWrite(UAV)之后由DeferredLighting读收敛
        RDGTextureHandle lightClusterGrid = builder.CreateTexture("Light Cluster Grid")
            .Import(EngineContext::RenderResource()->GetLightClusterGridTexture())
            .Finish();

        RDGBufferHandle lightClusterIndex = builder.CreateBuffer("Light Cluster Index")
            .Import(EngineContext::RenderResource()->GetLightClusterIndexBuffer(), RESOURCE_STATE_SHADER_RESOURCE)
            .Finish();

        RDGComputePassHandle pass = builder.CreateComputePass(GetName())
            .RootSignature(rootSignature)
            .ReadWrite(2, SET2_BINDING_LIGHT_CLUSTER_GRID, 0, lightClusterGrid, VIEW_TYPE_3D)  
            .ReadWrite(2, SET2_BINDING_LIGHT_CLUSTER_INDEX, 0, lightClusterIndex)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->Dispatch(  1, 
                                    1, 
                                    LIGHT_CLUSTER_DEPTH);
            })
            .OutputReadWrite(lightClusterGrid)
            .OutputReadWrite(lightClusterIndex)
            .Finish();
    }
}
