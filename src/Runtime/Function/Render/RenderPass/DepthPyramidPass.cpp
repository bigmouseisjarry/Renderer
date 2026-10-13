#include "DepthPyramidPass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <algorithm>
#include <cstdint>
#include <string>

void DepthPyramidPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader = Shader(EngineContext::File()->ShaderPath() + "depth/depth_pyramid.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry({0, 0, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})
                     .AddEntry({0, 1, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry(EngineContext::RenderResource()->GetSamplerRootSignature()->GetInfo())
                     .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;
    pipelineInfo.computeShader              = computeShader.shader;  
    computePipeline   = backend->CreateComputePipeline(pipelineInfo);
}   

void DepthPyramidPass::Build(RDGBuilder& builder)
{
    RDGTextureHandle depthMin = builder.GetOrCreateTexture("Depth Min")
        .Import(EngineContext::RenderResource()->GetDepthPyramidTexture(0), RESOURCE_STATE_SHADER_RESOURCE)
        .Finish();

    RDGTextureHandle depthMax = builder.GetOrCreateTexture("Depth Max")
        .Import(EngineContext::RenderResource()->GetDepthPyramidTexture(1), RESOURCE_STATE_SHADER_RESOURCE)
        .Finish();

    Build(builder, 0, depthMin);
    Build(builder, 1, depthMax);
}

void DepthPyramidPass::Build(RDGBuilder& builder, uint32_t mode, RDGTextureHandle depthMip)
{
    Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();
    int mipLevels = (std::floor(std::log2(std::max(windowExtent.width, windowExtent.height)))) + 1;

    RDGTextureHandle depth = builder.GetTexture("Depth");

    for(int i = 0; i < mipLevels; i++)
    {
        std::string minmax = (mode == 0) ? " Min" : " Max";
        std::string index = " [" + std::to_string(i) + "]";

        auto passBuilder = builder.CreateComputePass(GetName() + minmax + index)
            .PassIndex(i, mode)
            .AddFlag(RDGPassFlags::ForceGraphicsQueue)
            .RootSignature(rootSignature)
            .Read(0, 0, 0, 
                (i == 0) ? depth : depthMip, 
                VIEW_TYPE_2D, 
                { (i == 0) ? TEXTURE_ASPECT_DEPTH : TEXTURE_ASPECT_COLOR, (uint32_t)(std::max(i - 1, 0)), 1, 0, 1 })
            .ReadWrite(0, 1, 0, depthMip, VIEW_TYPE_2D, { TEXTURE_ASPECT_COLOR, (uint32_t)i, 1, 0, 1 })
            .Execute([&](RDGPassContext context) {       

                Extent2D extent = EngineContext::Render()->GetWindowsExtent();
                int mipLevel    = context.passIndex[0];
                extent.width    = std::max((int)std::ceil(extent.width / (std::pow(2, mipLevel) * 16)), 1);
                extent.height   = std::max((int)std::ceil(extent.height / (std::pow(2, mipLevel) * 16)), 1);

                DepthPyramidSetting passSetting = {};
                passSetting.mode = context.passIndex[1];
                passSetting.mipLevel = mipLevel;

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline);
                command->BindDescriptorSet(context.descriptors[0], 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetSamplerDescriptorSet(), 1);
                command->PushConstants(&passSetting, sizeof(DepthPyramidSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  extent.width,   // 已经除过local_size了
                                    extent.height, 
                                    1);
            });

        // 每级产出即收敛SRV：帧末全mip可采样（供下帧GPUCulling经per-frame set读旧内容——其虚拟
        // Read边按SRV导入，保内容转移的前提）。屏障数与旧"仅末级"方案持平：本级UAV→SRV的
        // OutputRead替代了下级pass读输入时的同款转移
        passBuilder.OutputRead(depthMip, { TEXTURE_ASPECT_COLOR, (uint32_t)i, 1, 0, 1 });
        passBuilder.Finish();
    }
}