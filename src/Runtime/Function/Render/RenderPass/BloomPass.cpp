#include "BloomPass.h"
#include "Core/Math/Math.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

void BloomPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader[0] = Shader(EngineContext::File()->ShaderPath() + "bloom/bloom_threshold.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[1] = Shader(EngineContext::File()->ShaderPath() + "bloom/bloom_down_sample.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[2] = Shader(EngineContext::File()->ShaderPath() + "bloom/bloom_up_sample.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[3] = Shader(EngineContext::File()->ShaderPath() + "bloom/bloom_combine.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry({0, 0, 2, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})
                     .AddEntry({0, 1, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry(EngineContext::RenderResource()->GetSamplerRootSignature()->GetInfo())
                     .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;

    for(int i = 0; i < 4; i++)
    {
        pipelineInfo.computeShader  = computeShader[i].shader; 
        computePipeline[i]          = backend->CreateComputePipeline(pipelineInfo);
    }
}   

void BloomPass::Build(RDGBuilder& builder) 
{
    Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();
    int mipLevels = windowExtent.MipSize();
    setting.maxMip = mipLevels - 1;

    RDGTextureHandle inputColor = builder.GetTexture("Mesh Pass Out Color");

    RDGTextureHandle outColor = builder.CreateTexture("Bloom Out Color") 
        .Exetent({windowExtent.width, windowExtent.height, 1})
        .Format(EngineContext::Render()->GetHdrColorFormat())
        .ArrayLayers(1)
        .MipLevels(1)
        .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
        .AllowReadWrite()
        .AllowRenderTarget()
        .Finish();  

    if( IsEnabled() && 
        !EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) &&
        !EngineContext::Render()->IsPassEnabled(RAY_TRACING_BASE_PASS) && 
        !EngineContext::Render()->IsPassEnabled(PATH_TRACING_PASS))
    {
        RDGTextureHandle downSampleMip = builder.CreateTexture("Bloom Down Sample Mip")
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(mipLevels)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .AllowRenderTarget()
            .Finish();  

        RDGTextureHandle upSampleMip = builder.CreateTexture("Bloom Up Sample Mip")
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(mipLevels)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .AllowRenderTarget()
            .Finish(); 

        // pass0 根据mesh pass的输出结果选取阈值
        RDGComputePassHandle pass0 = builder.CreateComputePass(GetName() + " Threshold")
            .RootSignature(rootSignature)
            .Read(0, 0, 0, inputColor)
            .ReadWrite(0, 1, 0, downSampleMip, VIEW_TYPE_2D, { TEXTURE_ASPECT_COLOR, 0, 1, 0, 1 })
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[0]);
                command->BindDescriptorSet(context.descriptors[0], 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetSamplerDescriptorSet(), 1);
                command->PushConstants(&setting, sizeof(BloomSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();

        // pass1 逐级下采样，每级使用前一级的结果
        for(int i = 1; i < mipLevels; i++)
        {
            std::string index = " [" + std::to_string(i) + "]";

            auto pass1Builder = builder.CreateComputePass(GetName() + " Down Sample" + index)
                .PassIndex(i)
                .RootSignature(rootSignature)
                .Read(0, 0, 0, downSampleMip, VIEW_TYPE_2D, { TEXTURE_ASPECT_COLOR, (uint32_t)(i - 1), 1, 0, 1 })
                .ReadWrite(0, 1, 0, downSampleMip, VIEW_TYPE_2D, { TEXTURE_ASPECT_COLOR, (uint32_t)i, 1, 0, 1 })
                .Execute([&](RDGPassContext context) {       

                    Extent2D extent = EngineContext::Render()->GetWindowsExtent();
                    int mipLevel    = context.passIndex[0];
                    extent.width    = std::max((int)std::ceil(extent.width / (std::pow(2, mipLevel) * 16)), 1);
                    extent.height   = std::max((int)std::ceil(extent.height / (std::pow(2, mipLevel) * 16)), 1);

                    BloomSetting passSetting = setting;
                    passSetting.mipLevel = mipLevel;

                    RHICommandListRef command = context.command; 
                    command->SetComputePipeline(computePipeline[1]);
                    command->BindDescriptorSet(context.descriptors[0], 0);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetSamplerDescriptorSet(), 1);
                    command->PushConstants(&passSetting, sizeof(BloomSetting), SHADER_FREQUENCY_COMPUTE);
                    command->Dispatch(  extent.width,   // 已经除过local_size了
                                        extent.height, 
                                        1);
                });

            if(i == mipLevels - 1) // 最后一级手动屏障 TODO
                pass1Builder.OutputRead(downSampleMip, { TEXTURE_ASPECT_COLOR, (uint32_t)i, 1, 0, 1 });
            pass1Builder.Finish();
        }

        // pass2 逐级上采样，每级使用前一级的结果以及下采样的整个mip
        for(int i = mipLevels - 2; i >= 0; i--) // 从倒数第二层开始计算
        {
            std::string index = " [" + std::to_string(i) + "]";

            RDGComputePassHandle pass2 = builder.CreateComputePass(GetName() + " Up Sample" + index)
                .PassIndex(i)
                .RootSignature(rootSignature)
                .Read(0, 0, 0, upSampleMip, VIEW_TYPE_2D, { TEXTURE_ASPECT_COLOR, (uint32_t)(i + 1), 1, 0, 1 })
                .Read(0, 0, 1, downSampleMip)   // 读整个下采样mip
                .ReadWrite(0, 1, 0, upSampleMip, VIEW_TYPE_2D, { TEXTURE_ASPECT_COLOR, (uint32_t)i, 1, 0, 1 })
                .Execute([&](RDGPassContext context) {       

                    Extent2D extent = EngineContext::Render()->GetWindowsExtent();
                    int mipLevel    = context.passIndex[0];
                    extent.width    = std::max((int)std::ceil(extent.width / (std::pow(2, mipLevel) * 16)), 1);
                    extent.height   = std::max((int)std::ceil(extent.height / (std::pow(2, mipLevel) * 16)), 1);

                    BloomSetting passSetting = setting;
                    passSetting.mipLevel = mipLevel;

                    RHICommandListRef command = context.command; 
                    command->SetComputePipeline(computePipeline[2]);
                    command->BindDescriptorSet(context.descriptors[0], 0);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetSamplerDescriptorSet(), 1);
                    command->PushConstants(&passSetting, sizeof(BloomSetting), SHADER_FREQUENCY_COMPUTE);
                    command->Dispatch(  extent.width,   // 已经除过local_size了
                                        extent.height, 
                                        1);
                })
                .Finish();
        }
        
        // pass3 合并结果
        RDGComputePassHandle pass3 = builder.CreateComputePass(GetName() + " Combine")
            .RootSignature(rootSignature)
            .Read(0, 0, 0, inputColor)
            .Read(0, 0, 1, upSampleMip, VIEW_TYPE_2D, { TEXTURE_ASPECT_COLOR, 0, 1, 0, 1 })
            .ReadWrite(0, 1, 0, outColor)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[3]);
                command->BindDescriptorSet(context.descriptors[0], 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetSamplerDescriptorSet(), 1);
                command->PushConstants(&setting, sizeof(BloomSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();
    } 
    else {
        
        RDGCopyPassHandle copyPass = builder.CreateCopyPass(GetName() + " Copy")
            .From(inputColor)
            .To(outColor)
            .Finish();
    }
    
}
