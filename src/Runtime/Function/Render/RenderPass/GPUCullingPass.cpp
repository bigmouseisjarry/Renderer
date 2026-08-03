#include "GPUCullingPass.h"
#include "Core/Util/IndexAlloctor.h"
#include "Function/Global/Definations.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/RenderStructs.h"
#include "MeshPass.h"
#include "RenderPass.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

void GPUCullingPass::Init()
{
    auto backend = EngineContext::RHI();

    cullingShader               = Shader(EngineContext::File()->ShaderPath() + "culling/culling.comp.spv", SHADER_FREQUENCY_COMPUTE);
    clusterGroupCullingShader   = Shader(EngineContext::File()->ShaderPath() + "culling/culling_cluster_group.comp.spv", SHADER_FREQUENCY_COMPUTE);
    clusterCullingShader        = Shader(EngineContext::File()->ShaderPath() + "culling/culling_cluster.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry({1, 0, 1024, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_BUFFER})     // IndirectMeshDrawDatas
                     .AddEntry({1, 1, 1024, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_BUFFER})     // IndirectMeshDrawCommands
                     .AddEntry({1, 2, 1024, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_BUFFER})     // IndirectClusterDrawDatas
                     .AddEntry({1, 3, 1024, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_BUFFER})     // IndirectClusterDrawCommands
                     .AddEntry({1, 4, 1024, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_BUFFER})     // IndirectClusterGroupDrawDatas
                     .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});
                                 
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;
    
    pipelineInfo.computeShader              = cullingShader.shader;
    cullingPipeline                         = backend->CreateComputePipeline(pipelineInfo);

    pipelineInfo.computeShader              = clusterGroupCullingShader.shader;
    clusterGroupCullingPipeline             = backend->CreateComputePipeline(pipelineInfo);
    
    pipelineInfo.computeShader              = clusterCullingShader.shader;
    clusterCullingPipeline                  = backend->CreateComputePipeline(pipelineInfo);
}   

void GPUCullingPass::Build(RDGBuilder& builder) 
{
    auto& passes = EngineContext::Render()->GetMeshPasses();

    lodSetting.enableStatistics = enableStatistics ? 1 : 0;
    // lodSetting.disableVirtualMeshCulling = 
    //     lodSetting.disableVirtualMeshCulling || (EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) ? true : false); //
    for(uint32_t i = 0; i < MESH_PASS_TYPE_MAX_CNT; i++)
    {
        if(!passes[i]) continue;

        std::string indexStrX = " [" + std::to_string(i) + "]";

        auto pass0Builder = builder.CreateComputePass(GetName() + " Mesh" + indexStrX);
        auto pass1Builder = builder.CreateComputePass(GetName() + " Cluster Group" + indexStrX);
        auto pass2Builder = builder.CreateComputePass(GetName() + " Cluster" + indexStrX);

        auto& processSizes = passes[i]->GetMeshPassProcessor()->GetProcessSizes();
        auto& passBuffers = passes[i]->GetMeshPassProcessor()->GetIndirectBuffers();
        uint32_t passCount = passBuffers.size();
        for(uint32_t j = 0; j < passCount; j++)
        {
            std::string indexStrXY = " [" + std::to_string(i) + "][" + std::to_string(j) + "]";

            RDGBufferHandle meshDrawDatas = builder.CreateBuffer("Mesh Draw Datas" + indexStrXY)
                .Import(passBuffers[j]->meshDrawDataBuffer.buffer, RESOURCE_STATE_UNDEFINED)
                .Finish();

            RDGBufferHandle meshDrawCommands = builder.CreateBuffer("Mesh Draw Commands" + indexStrXY)
                .Import(passBuffers[j]->meshDrawCommandBuffer.buffer, RESOURCE_STATE_UNDEFINED)
                .Finish();

            RDGBufferHandle clusrterDrawDatas = builder.CreateBuffer("Clusrter Draw Datas" + indexStrXY)
                .Import(passBuffers[j]->clusterDrawDataBuffer.buffer, RESOURCE_STATE_UNDEFINED)
                .Finish();

            RDGBufferHandle clusrterDrawCommands = builder.CreateBuffer("Clusrter Draw Commands" + indexStrXY)
                .Import(passBuffers[j]->clusterDrawCommandBuffer.buffer, RESOURCE_STATE_UNDEFINED)
                .Finish();

            RDGBufferHandle clusrterGroupDrawDatas = builder.CreateBuffer("Clusrter Group Draw Datas" + indexStrXY)
                .Import(passBuffers[j]->clusterGroupDrawDataBuffer.buffer, RESOURCE_STATE_UNDEFINED)
                .Finish();

            pass0Builder
                .ReadWrite(1, 0, j, meshDrawDatas)
                .ReadWrite(1, 1, j, meshDrawCommands);

            pass1Builder
                .ReadWrite(1, 2, j, clusrterDrawDatas)
                .ReadWrite(1, 4, j, clusrterGroupDrawDatas);

            pass2Builder
                .ReadWrite(1, 1, j, meshDrawCommands)
                .ReadWrite(1, 2, j, clusrterDrawDatas)
                .ReadWrite(1, 3, j, clusrterDrawCommands)            
                .OutputIndirectDraw(meshDrawCommands)       // 输出，作为后续间接绘制的buffer
                .OutputIndirectDraw(clusrterDrawCommands);
        }

        pass0Builder
            .RootSignature(rootSignature)
            .PassIndex(i, passCount, processSizes[0])
            .Execute([&](RDGPassContext context) {       

                CullingLodSetting setting = lodSetting;
                setting.passType = context.passIndex[0];
                setting.passCount = context.passIndex[1];

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(cullingPipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(context.descriptors[1], 1);
                command->PushConstants(&setting, sizeof(CullingLodSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  (context.passIndex[2] + 63) / 64, 
                                    1, 
                                    1); 
            })
            .Finish();

        pass1Builder
            .RootSignature(rootSignature)
            .PassIndex(i, passCount, processSizes[1])
            .Execute([&](RDGPassContext context) {       

                CullingLodSetting setting = lodSetting;
                setting.passType = context.passIndex[0];
                setting.passCount = context.passIndex[1];

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(clusterGroupCullingPipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(context.descriptors[1], 1);
                command->PushConstants(&setting, sizeof(CullingLodSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  (context.passIndex[2] + 63) / 64, 
                                    1, 
                                    1); 
            })
            .Finish();

        pass2Builder
            .RootSignature(rootSignature)
            .PassIndex(i, passCount, processSizes[2])
            .Execute([&](RDGPassContext context) {       

                CullingLodSetting setting = lodSetting;
                setting.passType = context.passIndex[0];
                setting.passCount = context.passIndex[1];

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(clusterCullingPipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(context.descriptors[1], 1);
                command->PushConstants(&setting, sizeof(CullingLodSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch( (context.passIndex[2] + 63) / 64, 
                                    1, 
                                    1);    
            })
            .Finish();
    }
}

void GPUCullingPass::CollectStatisticDatas()
{
    if(!enableStatistics) return;

    auto& passes = EngineContext::Render()->GetMeshPasses();

    for(uint32_t i = 0; i < MESH_PASS_TYPE_MAX_CNT; i++)
    {
        readBackDatas[i].clear();
        if(!passes[i]) continue;
        for(auto& indirectBuffer : passes[i]->GetMeshPassProcessor()->GetIndirectBuffers())
        {
            ReadBackData readBackData = {};
            indirectBuffer->meshDrawDataBuffer.GetData(&readBackData.meshSetting, sizeof(IndirectSetting), 0);
            indirectBuffer->clusterDrawDataBuffer.GetData(&readBackData.clusterSetting, sizeof(IndirectSetting), 0);
            indirectBuffer->clusterGroupDrawDataBuffer.GetData(&readBackData.clusterGroupSetting, sizeof(IndirectSetting), 0);
            readBackDatas[i].push_back(readBackData);
        }
    }
}
