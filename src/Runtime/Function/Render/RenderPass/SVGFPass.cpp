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
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo())       // 刀7b-3：set2全局统一RS（98槽）
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
        // 开启DI后，deferred流程被禁用，因此Noisy HDR Color里无可用数据

        Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

        RDGTextureHandle diffuse         = builder.GetTexture("G-Buffer Diffuse/Metallic");
        RDGTextureHandle normal          = builder.GetTexture("G-Buffer Normal/Roughness");
        RDGTextureHandle historyNormal   = builder.GetTexture("History G-Buffer Normal/Roughness");
        //RDGTextureHandle reprojectionOut = builder.GetTexture("Reprojection Out");
        RDGTextureHandle inColor        = builder.GetTexture("ReSTIR Diffuse Color");
        RDGTextureHandle outColor      = builder.GetTexture("Noisy HDR Color");

        // 稳态均=SRV：末位触碰者是最后一轮Wavelet的Read（color的copy发生在首轮后、moments的copy更早）
        RDGTextureHandle historyMoments = builder.CreateTexture("SVGF History Moments/History Length")
            .Import(historyMomentsTex)
            .Finish();

        RDGTextureHandle historyColor = builder.CreateTexture("SVGF History Color/Variance")
            .Import(historyColorTex)
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
            .Shaders({computeShader[0].shader})           // 刀7b-3：严格边校验
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 1, builder.GetTexture("Previous Depth"), VIEW_TYPE_2D, {})
            .ReadIfValid(2, SET2_BINDING_HISTORY_VELOCITY, 0, "G-Buffer Velocity")
            .ReadIfValid(2, SET2_BINDING_HISTORY_OBJECT_ID, 0, "G-Buffer Object ID")
            .ReadIfValid(2, SET2_BINDING_HISTORY_OBJECT_ID, 1, "History G-Buffer Object ID")
            .Read(2, SET2_BINDING_GBUFFER_DIFFUSE_METALLIC, 0, diffuse)
            .Read(2, SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS, 0, normal)
            .Read(2, SET2_BINDING_SVGF_HISTORY_GBUFFER_NORMAL, 0, historyNormal)
            .ReadWrite(2, SET2_BINDING_IN_COLOR, 0, inColor)
            .Read(2, SET2_BINDING_SVGF_HISTORY_MOMENTS, 0, historyMoments)
            .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_MOMENTS, 0, currentMoments)
            .Read(2, SET2_BINDING_SVGF_HISTORY_COLOR_VARIANCE, 0, historyColor)
            .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE, 0, currentColor0)
            .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE, 1, currentColor1, VIEW_TYPE_2D, {})
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[0]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);   
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->PushConstants(&setting, sizeof(SVGFSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();


        RDGComputePassHandle pass1 = builder.CreateComputePass(GetName() + " Disocclusion Fix")
            .RootSignature(rootSignature)
            .Shaders({computeShader[1].shader})           // 刀7b-3：严格边校验
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 1, builder.GetTexture("Previous Depth"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS, 0, normal)
            .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_MOMENTS, 0, currentMoments)
            .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE, 0, currentColor0)
            .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE, 1, currentColor1, VIEW_TYPE_2D, {})
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[1]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);   
                command->BindDescriptorSet(context.descriptors[2], 2);
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
            .Shaders({computeShader[2].shader})           // 刀7b-3：严格边校验
            .ReadIfValid(2, SET2_BINDING_HISTORY_OBJECT_ID, 0, "G-Buffer Object ID")
            .ReadIfValid(2, SET2_BINDING_HISTORY_OBJECT_ID, 1, "History G-Buffer Object ID")
            .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE, 0, currentColor0)
            .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE, 1, currentColor1, VIEW_TYPE_2D, {})
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[2]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);   
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->PushConstants(&setting, sizeof(SVGFSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();

        RDGComputePassHandle pass3 = builder.CreateComputePass(GetName() + " Variance Estimation")
            .RootSignature(rootSignature)
            .Shaders({computeShader[3].shader})           // 刀7b-3：严格边校验
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 1, builder.GetTexture("Previous Depth"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS, 0, normal)
            .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_MOMENTS, 0, currentMoments)
            .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE, 0, currentColor0)
            .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE, 1, currentColor1, VIEW_TYPE_2D, {})
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[3]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);   
                command->BindDescriptorSet(context.descriptors[2], 2);
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
            //     .Read(2, SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS, 0, normal)
            //     .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE, 0, currentColor0)
            //     .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE, 1, currentColor1, VIEW_TYPE_2D, {})
            //     .Execute([&](RDGPassContext context) {       

            //         SVGFSetting newSetting = setting;
            //         newSetting.round = context.passIndex[0];

            //         RHICommandListRef GraphicsCommand = context.GraphicsCommand; 
            //         GraphicsCommand->SetComputePipeline(computePipeline[4]);
            //         GraphicsCommand->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);   
            //         GraphicsCommand->BindDescriptorSet(context.descriptors[2], 2);
            //         GraphicsCommand->PushConstants(&newSetting, sizeof(SVGFSetting), SHADER_FREQUENCY_COMPUTE);
            //         GraphicsCommand->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
            //                             Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
            //                             1);
            //     })
            //     .Finish();

            RDGComputePassHandle pass5 = builder.CreateComputePass(GetName() + " Wavelet [" + std::to_string(i) + "]")
                .PassIndex(i)
                .RootSignature(rootSignature)
                .Shaders({computeShader[5].shader})           // 刀7b-3：严格边校验
                .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))
                .Read(2, SET2_BINDING_HISTORY_DEPTH, 1, builder.GetTexture("Previous Depth"), VIEW_TYPE_2D, {})
                .Read(2, SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS, 0, normal)
                .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE, 0, currentColor0)
                .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE, 1, currentColor1, VIEW_TYPE_2D, {})
                .Execute([&](RDGPassContext context) {       

                    SVGFSetting newSetting = setting;
                    newSetting.round = context.passIndex[0];

                    RHICommandListRef command = context.command; 
                    command->SetComputePipeline(computePipeline[5]);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);   
                    command->BindDescriptorSet(context.descriptors[2], 2);
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
            .Shaders({computeShader[6].shader})           // 刀7b-3：严格边校验
            .Read(2, SET2_BINDING_GBUFFER_DIFFUSE_METALLIC, 0, diffuse)
            .ReadWrite(2, SET2_BINDING_IN_COLOR, 0, inColor)
            .ReadWrite(2, SET2_BINDING_OUT_COLOR, 0, outColor)
            .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_MOMENTS, 0, currentMoments)
            .Read(2, SET2_BINDING_SVGF_HISTORY_COLOR_VARIANCE, 0, historyColor)
            .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE, 0, currentColor0)
            .ReadWrite(2, SET2_BINDING_SVGF_CURRENT_COLOR_VARIANCE, 1, currentColor1, VIEW_TYPE_2D, {})
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[6]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);   
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->PushConstants(&setting, sizeof(SVGFSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();
    }

}
