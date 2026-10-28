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
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo())       
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

    RDGBufferHandle gizmoData = builder.GetOrCreateBuffer("Gizmo Data")
        .Import(EngineContext::RenderResource()->GetGizmoDataBuffer(), RESOURCE_STATE_INDIRECT_ARGUMENT)
        .Finish();

    auto& passes = EngineContext::Render()->GetMeshPasses();

    lodSetting.enableStatistics = enableStatistics ? 1 : 0;
    // lodSetting.disableVirtualMeshCulling = 
    //     lodSetting.disableVirtualMeshCulling || (EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) ? true : false); //


    RDGTextureHandle depthMax = builder.GetOrCreateTexture("Depth Max")
        .Import(EngineContext::RenderResource()->GetDepthPyramidTexture(1))
        .Finish();

    // 遍历每一个标记为 Mesh_Pass 的 pass
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
        // 该种类的pass需要跑多少遍
        for(uint32_t j = 0; j < passCount; j++)
        {
            std::string indexStrXY = " [" + std::to_string(i) + "][" + std::to_string(j) + "]";

            RDGBufferHandle meshDrawDatas = builder.CreateBuffer("Mesh Draw Datas" + indexStrXY)
                .Import(passBuffers[j]->meshDrawDataBuffer.buffer, RESOURCE_STATE_UNORDERED_ACCESS)
                .Finish();

            RDGBufferHandle meshDrawCommands = builder.CreateBuffer("Mesh Draw Commands" + indexStrXY)
                .Import(passBuffers[j]->meshDrawCommandBuffer.buffer, RESOURCE_STATE_INDIRECT_ARGUMENT)
                .Finish();

            RDGBufferHandle clusrterDrawDatas = builder.CreateBuffer("Clusrter Draw Datas" + indexStrXY)
                .Import(passBuffers[j]->clusterDrawDataBuffer.buffer, RESOURCE_STATE_UNORDERED_ACCESS)
                .Finish();

            RDGBufferHandle clusrterDrawCommands = builder.CreateBuffer("Clusrter Draw Commands" + indexStrXY)
                .Import(passBuffers[j]->clusterDrawCommandBuffer.buffer, RESOURCE_STATE_INDIRECT_ARGUMENT)
                .Finish();

            RDGBufferHandle clusrterGroupDrawDatas = builder.CreateBuffer("Clusrter Group Draw Datas" + indexStrXY)
                .Import(passBuffers[j]->clusterGroupDrawDataBuffer.buffer, RESOURCE_STATE_UNORDERED_ACCESS)
                .Finish();

            // 稳态=SRV：正常帧末位触碰者=各mesh render pass的读；无读者帧（级联不更新/光源禁用/RT模式）=pass2的OutputRead收敛——两种帧形都闭环
            RDGBufferHandle clusterDrawInfos = builder.CreateBuffer("Cluster Draw Infos" + indexStrXY)
                .Import(passBuffers[j]->clusterDrawInfoBuffer, RESOURCE_STATE_SHADER_RESOURCE)
                .Finish();

            pass0Builder
                // 现行犯#1修复(2026-10-28悬空槽案): culling.comp:58静态读DEPTH_PYRAMID[1](=Max,遮蔽
                // 剔除语义),原绑[0](=Min)为索引错位——Mesh剔除每帧每物体读悬空槽=按物体闪烁噪声+垃圾读彩票
                .Read(2, SET2_BINDING_HISTORY_DEPTH_PYRAMID, 1, depthMax, VIEW_TYPE_2D, {})

                .ReadWrite(2, SET2_BINDING_GIZMO_DRAW_DATA, 0, gizmoData, 0, 0)
                .ReadWrite(2, SET2_BINDING_INDIRECT_MESH_DRAW_DATAS, j, meshDrawDatas, 0, 0)
                .ReadWrite(2, SET2_BINDING_INDIRECT_MESH_DRAW_COMMANDS, j, meshDrawCommands, 0, 0)
                .OutputIndirectDraw(meshDrawCommands);       // 输出，作为后续间接绘制的buffer。

            pass1Builder
                .ReadWrite(2, SET2_BINDING_INDIRECT_CLUSTER_DRAW_DATAS, j, clusrterDrawDatas, 0, 0)
                .ReadWrite(2, SET2_BINDING_INDIRECT_CLUSTER_GROUP_DATAS, j, clusrterGroupDrawDatas, 0, 0);

            pass2Builder
                .Read(2, SET2_BINDING_HISTORY_DEPTH_PYRAMID, 1, depthMax, VIEW_TYPE_2D, {})
                .ReadWrite(2, SET2_BINDING_GIZMO_DRAW_DATA, 0, gizmoData, 0, 0)
                .ReadWrite(2, SET2_BINDING_INDIRECT_CLUSTER_DRAW_DATAS, j, clusrterDrawDatas, 0, 0)
                .ReadWrite(2, SET2_BINDING_INDIRECT_CLUSTER_DRAW_COMMANDS, j, clusrterDrawCommands, 0, 0)
                .ReadWrite(2, SET2_BINDING_CLUSTER_DRAW_INFOS_WRITE, j, clusterDrawInfos, 0, 0)                 // 剔除后实际绘制的cluster索引表（本遍自己的那块）
                .OutputIndirectDraw(clusrterDrawCommands)    // 输出，作为后续间接绘制的buffer
                .OutputRead(clusterDrawInfos);
        }

        pass0Builder
            .RootSignature(rootSignature)
            .Shaders({cullingShader.shader})               
			.PassIndex(i, passCount, processSizes[0])           // pass类型 、 该类型跑多少遍 、每遍处理多少个mesh
            .Execute([&](RDGPassContext context) {       

                CullingLodSetting setting = lodSetting;
                setting.passType = context.passIndex[0];
                setting.passCount = context.passIndex[1];

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(cullingPipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->PushConstants(&setting, sizeof(CullingLodSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  std::ceil(context.passIndex[2] / 64.0),                                   // 64个一组
                                    1, 
                                    1); 
            })
            .Finish();

        pass1Builder
            .RootSignature(rootSignature)
            .Shaders({clusterGroupCullingShader.shader})   
            .PassIndex(i, passCount, processSizes[1])
            .Execute([&](RDGPassContext context) {       

                CullingLodSetting setting = lodSetting;
                setting.passType = context.passIndex[0];
                setting.passCount = context.passIndex[1];

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(clusterGroupCullingPipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->PushConstants(&setting, sizeof(CullingLodSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  std::ceil(context.passIndex[2] / 64.0), 
                                    1, 
                                    1); 
            })
            .Finish();

        pass2Builder
            .RootSignature(rootSignature)
            .Shaders({clusterCullingShader.shader})         
            .PassIndex(i, passCount, processSizes[2])
            .Execute([&](RDGPassContext context) {       

                CullingLodSetting setting = lodSetting;
                setting.passType = context.passIndex[0];
                setting.passCount = context.passIndex[1];

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(clusterCullingPipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->PushConstants(&setting, sizeof(CullingLodSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch( std::ceil(context.passIndex[2] / 64.0), 
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
