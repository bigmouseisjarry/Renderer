#include "ReSTIRDIPass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderPass/RenderPass.h"
#include <cstdint>

void ReSTIRDIPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader[0] = Shader(EngineContext::File()->ShaderPath() + "restir_di/temporal_reuse.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[1] = Shader(EngineContext::File()->ShaderPath() + "restir_di/spatial_reuse.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[2] = Shader(EngineContext::File()->ShaderPath() + "restir_di/lighting.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo())       // 刀7b-3：set2全局统一RS（102槽）
                                                                                                                                                                                                                                                                                                                                                .AddPushConstant({128, SHADER_FREQUENCY_RAY_TRACING | SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    // 刀2b边化：reservoirs/setting不再自管描述符集，经RDG边并入池化set2（b32-35），
    // 乒乓parity改由边上的buffer映射表达（见Build）

    RHIComputePipelineInfo compPipelineInfo     = {};
    compPipelineInfo.rootSignature              = rootSignature;

    compPipelineInfo.computeShader              = computeShader[0].shader;
    computePipeline[0] = backend->CreateComputePipeline(compPipelineInfo);

    compPipelineInfo.computeShader              = computeShader[1].shader;
    computePipeline[1] = backend->CreateComputePipeline(compPipelineInfo);

    compPipelineInfo.computeShader              = computeShader[2].shader;
    computePipeline[2] = backend->CreateComputePipeline(compPipelineInfo);
}   

void ReSTIRDIPass::Build(RDGBuilder& builder) 
{
    if( IsEnabled() && 
        !EngineContext::Render()->IsPassEnabled(PATH_TRACING_PASS))
    {
        currentIndex = (currentIndex + 1) % 2;

        Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

        RDGTextureHandle diffuse         = builder.GetTexture("G-Buffer Diffuse/Metallic");
        RDGTextureHandle normal          = builder.GetTexture("G-Buffer Normal/Roughness");
        RDGTextureHandle emission        = builder.GetTexture("G-Buffer Emission");
        RDGTextureHandle specularF0      = builder.GetTexture("G-Buffer Specular F0/Roughness");
        RDGTextureHandle reprojectionOut = builder.GetTexture("Reprojection Out");
        RDGTextureHandle outColor        = builder.GetTexture("Noisy HDR Color");

        RDGTextureHandle restirDiffuseColor = builder.GetOrCreateTexture("ReSTIR Diffuse Color") 
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .AllowRenderTarget()
            .Finish();
                
        RDGTextureHandle restirSpecularColor = builder.GetOrCreateTexture("ReSTIR Specular Color") 
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .AllowRenderTarget()
            .Finish();

        setting.combineMode = (EngineContext::Render()->IsPassEnabled(SVGF_PASS) ||
                               EngineContext::Render()->IsPassEnabled(NRD_PASS)) ? 1 : 0;
        restirSettingBuffer.SetData(setting);

        // 刀2b边化：四buffer入图（稳态=UAV），乒乓parity=currentIndex选prev/current映射
        RDGBufferHandle settingBuf = builder.GetOrCreateBuffer(GetName() + " Setting")
            .Import(restirSettingBuffer.buffer, RESOURCE_STATE_UNORDERED_ACCESS)
            .Finish();
        RDGBufferHandle reservoirBuf[3];
        for(uint32_t i = 0; i < 3; i++)
            reservoirBuf[i] = builder.GetOrCreateBuffer(GetName() + " Reservoirs [" + std::to_string(i) + "]")
                .Import(reservoirsBuffer[i].buffer, RESOURCE_STATE_UNORDERED_ACCESS)
                .Finish();
        // b33=当前帧reservoirs(rb0恒定)，b34/b35=输入/输出对（parity交换，等价旧descriptorSet[parity]）
        RDGBufferHandle reservoirA = currentIndex == 0 ? reservoirBuf[1] : reservoirBuf[2];
        RDGBufferHandle reservoirB = currentIndex == 0 ? reservoirBuf[2] : reservoirBuf[1];

        RDGComputePassHandle pass0 = builder.CreateComputePass(GetName() + " Temporal Reuse")
            .PassIndex(currentIndex)
            .RootSignature(rootSignature)
            .Shaders({computeShader[0].shader})           // 刀7b-3：严格边校验
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_GBUFFER_DIFFUSE, 0, diffuse)
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_GBUFFER_NORMAL, 0, normal)
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_GBUFFER_EMISSION, 0, emission)
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_GBUFFER_SPECF0, 0, specularF0)
            .Read(2, SET2_BINDING_REPROJECTION_RESULT, 0, reprojectionOut)
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_SETTING, 0, settingBuf, 0, 0)
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 1, builder.GetTexture("Previous Depth"), VIEW_TYPE_2D, {})
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_RESERVOIRS, 0, reservoirBuf[0], 0, 0)
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_PREV_RESERVOIRS, 0, reservoirA)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[0]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);  
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();

        RDGComputePassHandle pass1 = builder.CreateComputePass(GetName() + " Spatial Reuse")
            .PassIndex(currentIndex)
            .RootSignature(rootSignature)
            .Shaders({computeShader[1].shader})           // 刀7b-3：严格边校验
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))    // 假边扫描补：spatial_reuse accessed b2原缺边
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_GBUFFER_DIFFUSE, 0, diffuse)
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_GBUFFER_NORMAL, 0, normal)
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_GBUFFER_EMISSION, 0, emission)
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_GBUFFER_SPECF0, 0, specularF0)
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_SETTING, 0, settingBuf, 0, 0)
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_RESERVOIRS, 0, reservoirBuf[0], 0, 0)
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_RESULT_RESERVOIRS, 0, reservoirB)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[1]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);  
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();

        RDGComputePassHandle pass2 = builder.CreateComputePass(GetName() + " Lighting")
            .PassIndex(currentIndex)
            .RootSignature(rootSignature)
            .Shaders({computeShader[2].shader})           // 刀7b-3：严格边校验
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_GBUFFER_DIFFUSE, 0, diffuse)
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_GBUFFER_NORMAL, 0, normal)
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_GBUFFER_EMISSION, 0, emission)
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_GBUFFER_SPECF0, 0, specularF0)
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_SETTING, 0, settingBuf, 0, 0)
            .ReadWrite(2, SET2_BINDING_RESTIR_DI_RESULT_RESERVOIRS, 0, reservoirB, 0, 0)
            .ReadWrite(2, SET2_BINDING_FINAL_COLOR, 0, outColor)
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 1, builder.GetTexture("Previous Depth"), VIEW_TYPE_2D, {})
            .ReadWrite(2, SET2_BINDING_DI_DIFFUSE_COLOR, 0, restirDiffuseColor)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[2]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();
    }
}
