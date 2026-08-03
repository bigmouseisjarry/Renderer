#include "SVGFPass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "RenderPass.h"
#include <cstdint>
#include <string>

void SVGFPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader[0] = Shader(EngineContext::File()->ShaderPath() + "svgf/temporal_accumulation.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[1] = Shader(EngineContext::File()->ShaderPath() + "svgf/disocclusion_fix.comp.spv", SHADER_FREQUENCY_COMPUTE);        // ReLAX
    computeShader[2] = Shader(EngineContext::File()->ShaderPath() + "svgf/anti_firefly.comp.spv", SHADER_FREQUENCY_COMPUTE);            // ReLAX
    computeShader[3] = Shader(EngineContext::File()->ShaderPath() + "svgf/variance_estimation.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[4] = Shader(EngineContext::File()->ShaderPath() + "svgf/atrous_variance_filter.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[5] = Shader(EngineContext::File()->ShaderPath() + "svgf/atrous_denoise.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[6] = Shader(EngineContext::File()->ShaderPath() + "svgf/combine.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry({1, 0, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})      // G_BUFFER_DIFFUSE_METALLIC
                     .AddEntry({1, 1, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})      // G_BUFFER_NORMAL_ROUGHNESS
                     .AddEntry({1, 2, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})      // HISTORY_G_BUFFER_NORMAL_ROUGHNESS
                     .AddEntry({1, 3, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})   // IN_COLOR
                     .AddEntry({1, 4, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})   // OUT_COLOR
                     .AddEntry({1, 5, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})      // HISTORY_MOMENTS
                     .AddEntry({1, 6, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})   // CURRENT_MOMENTS
                     .AddEntry({1, 7, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})      // HISTORY_COLOR_VARIANCE
                     .AddEntry({1, 8, 2, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})   // CURRENT_COLOR_VARIANCE[2]
                     .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;
    for(int i = 0; i < 7; i++)
    {
        pipelineInfo.computeShader          = computeShader[i].shader;
        computePipeline[i]   = backend->CreateComputePipeline(pipelineInfo);
    }

    Extent2D extent = EngineContext::Render()->GetWindowsExtent();
    historyMomentsTex = EngineContext::RHI()->CreateTexture({
        .format = FORMAT_R32G32B32A32_SFLOAT,
        .extent = {extent.width, extent.height, 1},
        .arrayLayers = 1,
        .mipLevels = 1,
        .memoryUsage = MEMORY_USAGE_GPU_ONLY,
        .type = RESOURCE_TYPE_RW_TEXTURE | RESOURCE_TYPE_TEXTURE
    });
    historyColorTex = EngineContext::RHI()->CreateTexture({
        .format = EngineContext::Render()->GetHdrColorFormat(),
        .extent = {extent.width, extent.height, 1},
        .arrayLayers = 1,
        .mipLevels = 1,
        .memoryUsage = MEMORY_USAGE_GPU_ONLY,
        .type = RESOURCE_TYPE_RW_TEXTURE | RESOURCE_TYPE_TEXTURE
    });
    currentColorTex[0] = EngineContext::RHI()->CreateTexture({
        .format = EngineContext::Render()->GetHdrColorFormat(),
        .extent = {extent.width, extent.height, 1},
        .arrayLayers = 1,
        .mipLevels = 1,
        .memoryUsage = MEMORY_USAGE_GPU_ONLY,
        .type = RESOURCE_TYPE_RW_TEXTURE | RESOURCE_TYPE_TEXTURE
    });
    currentColorTex[1] = EngineContext::RHI()->CreateTexture({
        .format = EngineContext::Render()->GetHdrColorFormat(),
        .extent = {extent.width, extent.height, 1},
        .arrayLayers = 1,
        .mipLevels = 1,
        .memoryUsage = MEMORY_USAGE_GPU_ONLY,
        .type = RESOURCE_TYPE_RW_TEXTURE | RESOURCE_TYPE_TEXTURE
    });
}   

void SVGFPass::Build(RDGBuilder& builder) 
{
    if( IsEnabled() &&
        (EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) ||
         EngineContext::Render()->IsPassEnabled(RESTIR_GI_PASS)) &&
        !EngineContext::Render()->IsPassEnabled(PATH_TRACING_PASS))
    {
        setting.denoisedOnly = (EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) || denoisedOnly) ? 1 : 0;   
        // 开启DI后，deferred流程被禁用，因此Mesh Pass Out Color里无可用数据

        Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

        RDGTextureHandle diffuse         = builder.GetTexture("G-Buffer Diffuse/Metallic");
        RDGTextureHandle normal          = builder.GetTexture("G-Buffer Normal/Roughness");
        RDGTextureHandle historyNormal   = builder.GetTexture("History G-Buffer Normal/Roughness");
        //RDGTextureHandle reprojectionOut = builder.GetTexture("Reprojection Out");
        RDGTextureHandle inColor        = builder.GetTexture("ReSTIR Diffuse Color");
        RDGTextureHandle outColor      = builder.GetTexture("Mesh Pass Out Color");

        RDGTextureHandle historyMoments = builder.CreateTexture("SVGF History Moments/History Length")
            .Import(historyMomentsTex, RESOURCE_STATE_UNDEFINED)
            .Finish();

        RDGTextureHandle historyColor = builder.CreateTexture("SVGF History Color/Variance")
            .Import(historyColorTex, RESOURCE_STATE_UNDEFINED)
            .Finish();

        RDGTextureHandle currentMoments = builder.CreateTexture("SVGF Current Moments/History Length")
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(FORMAT_R32G32B32A32_SFLOAT)
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();  

        RDGTextureHandle currentColor0 = builder.CreateTexture("SVGF Current Color/Variance [0]")
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();  

        RDGTextureHandle currentColor1 = builder.CreateTexture("SVGF Current Color/Variance [1]")
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();

        RDGComputePassHandle pass0 = builder.CreateComputePass(GetName() + " Temporal Accumulation")
            .RootSignature(rootSignature)
            .Read(1, 0, 0, diffuse)
            .Read(1, 1, 0, normal)
            .Read(1, 2, 0, historyNormal)
            .ReadWrite(1, 3, 0, inColor)
            .ReadWrite(1, 4, 0, outColor)
            .Read(1, 5, 0, historyMoments)
            .ReadWrite(1, 6, 0, currentMoments)
            .Read(1, 7, 0, historyColor)
            .ReadWrite(1, 8, 0, currentColor0)
            .ReadWrite(1, 8, 1, currentColor1)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[0]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);   
                command->BindDescriptorSet(context.descriptors[1], 1);
                command->PushConstants(&setting, sizeof(SVGFSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();


        RDGComputePassHandle pass1 = builder.CreateComputePass(GetName() + " Disocclusion Fix")
            .RootSignature(rootSignature)
            .Read(1, 0, 0, diffuse)
            .Read(1, 1, 0, normal)
            .Read(1, 2, 0, historyNormal)
            .ReadWrite(1, 3, 0, inColor)
            .ReadWrite(1, 4, 0, outColor)
            .Read(1, 5, 0, historyMoments)
            .ReadWrite(1, 6, 0, currentMoments)
            .Read(1, 7, 0, historyColor)
            .ReadWrite(1, 8, 0, currentColor0)
            .ReadWrite(1, 8, 1, currentColor1)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[1]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);   
                command->BindDescriptorSet(context.descriptors[1], 1);
                command->PushConstants(&setting, sizeof(SVGFSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();


        RDGCopyPassHandle copyPass0 = builder.CreateCopyPass(GetName() + " Copy Moments")    // 拷贝历史moments信息
            .From(currentMoments)
            .To(historyMoments)
            .Finish();

        RDGComputePassHandle pass2 = builder.CreateComputePass(GetName() + " Anti-Firefly")
            .RootSignature(rootSignature)
            .Read(1, 0, 0, diffuse)
            .Read(1, 1, 0, normal)
            .Read(1, 2, 0, historyNormal)
            .ReadWrite(1, 3, 0, inColor)
            .ReadWrite(1, 4, 0, outColor)
            .Read(1, 5, 0, historyMoments)
            .ReadWrite(1, 6, 0, currentMoments)
            .Read(1, 7, 0, historyColor)
            .ReadWrite(1, 8, 0, currentColor0)
            .ReadWrite(1, 8, 1, currentColor1)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[2]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);   
                command->BindDescriptorSet(context.descriptors[1], 1);
                command->PushConstants(&setting, sizeof(SVGFSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();

        RDGComputePassHandle pass3 = builder.CreateComputePass(GetName() + " Variance Estimation")
            .RootSignature(rootSignature)
            .Read(1, 0, 0, diffuse)
            .Read(1, 1, 0, normal)
            .Read(1, 2, 0, historyNormal)
            .ReadWrite(1, 3, 0, inColor)
            .ReadWrite(1, 4, 0, outColor)
            .Read(1, 5, 0, historyMoments)
            .ReadWrite(1, 6, 0, currentMoments)
            .Read(1, 7, 0, historyColor)
            .ReadWrite(1, 8, 0, currentColor0)
            .ReadWrite(1, 8, 1, currentColor1)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[3]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);   
                command->BindDescriptorSet(context.descriptors[1], 1);
                command->PushConstants(&setting, sizeof(SVGFSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();

        for(int i = 0; i < setting.maxRound; i++)
        {
            // RDGComputePassHandle pass4 = builder.CreateComputePass(GetName() + " Wavelet [" + std::to_string(i) + "] Pre-filter variance")
            //     .PassIndex(i)
            //     .RootSignature(rootSignature)
            //     .Read(1, 0, 0, diffuse)
            //     .Read(1, 1, 0, normal)
            //     .Read(1, 2, 0, historyNormal)
            //     .ReadWrite(1, 3, 0, inColor)
            //     .ReadWrite(1, 4, 0, outColor)
            //     .Read(1, 5, 0, historyMoments)
            //     .ReadWrite(1, 6, 0, currentMoments)
            //     .Read(1, 7, 0, historyColor)
            //     .ReadWrite(1, 8, 0, currentColor0)
            //     .ReadWrite(1, 8, 1, currentColor1)
            //     .Execute([&](RDGPassContext context) {       

            //         SVGFSetting newSetting = setting;
            //         newSetting.round = context.passIndex[0];

            //         RHICommandListRef command = context.command; 
            //         command->SetComputePipeline(computePipeline[4]);
            //         command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);   
            //         command->BindDescriptorSet(context.descriptors[1], 1);
            //         command->PushConstants(&newSetting, sizeof(SVGFSetting), SHADER_FREQUENCY_COMPUTE);
            //         command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
            //                             Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
            //                             1);
            //     })
            //     .Finish();

            RDGComputePassHandle pass5 = builder.CreateComputePass(GetName() + " Wavelet [" + std::to_string(i) + "]")
                .PassIndex(i)
                .RootSignature(rootSignature)
                .Read(1, 0, 0, diffuse)
                .Read(1, 1, 0, normal)
                .Read(1, 2, 0, historyNormal)
                .ReadWrite(1, 3, 0, inColor)
                .ReadWrite(1, 4, 0, outColor)
                .Read(1, 5, 0, historyMoments)
                .ReadWrite(1, 6, 0, currentMoments)
                .Read(1, 7, 0, historyColor)
                .ReadWrite(1, 8, 0, currentColor0)
                .ReadWrite(1, 8, 1, currentColor1)
                .Execute([&](RDGPassContext context) {       

                    SVGFSetting newSetting = setting;
                    newSetting.round = context.passIndex[0];

                    RHICommandListRef command = context.command; 
                    command->SetComputePipeline(computePipeline[5]);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);   
                    command->BindDescriptorSet(context.descriptors[1], 1);
                    command->PushConstants(&newSetting, sizeof(SVGFSetting), SHADER_FREQUENCY_COMPUTE);
                    command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                        Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                        1);
                })
                .Finish();

            if(i == 0)  // 拷贝历史color信息，使用首次循环的结果
            {
                RDGCopyPassHandle copyPass1 = builder.CreateCopyPass(GetName() + " Copy Color")
                    .From(currentColor1)
                    .To(historyColor)
                    .Finish();
            }
        }

        RDGComputePassHandle pass6 = builder.CreateComputePass(GetName() + " Combine")
            .RootSignature(rootSignature)
            .Read(1, 0, 0, diffuse)
            .Read(1, 1, 0, normal)
            .Read(1, 2, 0, historyNormal)
            .ReadWrite(1, 3, 0, inColor)
            .ReadWrite(1, 4, 0, outColor)
            .Read(1, 5, 0, historyMoments)
            .ReadWrite(1, 6, 0, currentMoments)
            .Read(1, 7, 0, historyColor)
            .ReadWrite(1, 8, 0, currentColor0)
            .ReadWrite(1, 8, 1, currentColor1)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[6]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);   
                command->BindDescriptorSet(context.descriptors[1], 1);
                command->PushConstants(&setting, sizeof(SVGFSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();
    }

}
