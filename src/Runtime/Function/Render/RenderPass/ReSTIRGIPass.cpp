#include "ReSTIRGIPass.h"
#include "Core/Math/Math.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderPass/RenderPass.h"
#include <cstdint>

#define NUM_NEIGHBORS 1

void ReSTIRGIPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader[0] = Shader(EngineContext::File()->ShaderPath() + "restir_gi/temporal_reuse.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[1] = Shader(EngineContext::File()->ShaderPath() + "restir_gi/spatial_reuse.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[2] = Shader(EngineContext::File()->ShaderPath() + "restir_gi/lighting.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo())       // 刀7b-3：set2全局统一RS（102槽）
                                                                                                                                                                                                                                                                                                                           .AddPushConstant({128, SHADER_FREQUENCY_RAY_TRACING | SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    // 刀2b边化：reservoirs/setting经RDG边并入池化set2（b32-35），自管描述符集已删除

    RHIComputePipelineInfo compPipelineInfo     = {};
    compPipelineInfo.rootSignature              = rootSignature;

    compPipelineInfo.computeShader              = computeShader[0].shader;
    computePipeline[0] = backend->CreateComputePipeline(compPipelineInfo);

    compPipelineInfo.computeShader              = computeShader[1].shader;
    computePipeline[1] = backend->CreateComputePipeline(compPipelineInfo);

    compPipelineInfo.computeShader              = computeShader[2].shader;
    computePipeline[2] = backend->CreateComputePipeline(compPipelineInfo);
}   

void ReSTIRGIPass::Build(RDGBuilder& builder) 
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

        // buffer稳态均=UAV：三pass的ReadWrite后由末pass的OutputReadWrite收敛UAV
        RDGBufferHandle resBuffer0 = builder.CreateBuffer("Reservoir Buffer 0")
            .Import(reservoirsBuffer[0].buffer, RESOURCE_STATE_UNORDERED_ACCESS)
            .Finish();

        RDGBufferHandle resBuffer1 = builder.CreateBuffer("Reservoir Buffer 1")
            .Import(reservoirsBuffer[1].buffer, RESOURCE_STATE_UNORDERED_ACCESS)
            .Finish();

        RDGBufferHandle resBuffer2 = builder.CreateBuffer("Reservoir Buffer 2")
            .Import(reservoirsBuffer[2].buffer, RESOURCE_STATE_UNORDERED_ACCESS)
            .Finish();

        // 刀2b边化补全：setting入图 + parity映射（等价旧descriptorSet[parity]的b2/b3交换）
        RDGBufferHandle settingBuf = builder.GetOrCreateBuffer(GetName() + " Setting")
            .Import(restirSettingBuffer.buffer, RESOURCE_STATE_UNORDERED_ACCESS)
            .Finish();
        RDGBufferHandle reservoirA = currentIndex == 0 ? resBuffer1 : resBuffer2;
        RDGBufferHandle reservoirB = currentIndex == 0 ? resBuffer2 : resBuffer1;

        setting.combineMode = !(EngineContext::Render()->IsPassEnabled(SVGF_PASS) ||
                                EngineContext::Render()->IsPassEnabled(NRD_PASS)) ? 0 :
                               !EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) ? 1 : 2;
        restirSettingBuffer.SetData(setting);

        // 刀5：mesh card readback经SC域b27（FetchSurfaceCacheLighting写时间戳；与PostProcessing共享"Surface Cache Card Update"节点）
        RDGBufferHandle cardUpdate = builder.GetOrCreateBuffer("Surface Cache Card Update")
            .Import(EngineContext::RenderResource()->GetCardUpdateBuffer(), RESOURCE_STATE_UNORDERED_ACCESS)
            .Finish();

        RDGComputePassHandle pass0 = builder.CreateComputePass(GetName() + " Temporal Reuse")
            .PassIndex(currentIndex)
            .RootSignature(rootSignature)
            .Shaders({computeShader[0].shader})           // 刀7b-3：严格边校验
            .ReadWrite(2, SET2_BINDING_MESH_CARD_READBACK, 0, cardUpdate)
            .Read(2, SET2_BINDING_GBUFFER_DIFFUSE_METALLIC, 0, diffuse)
            .Read(2, SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS, 0, normal)
            // 现行犯#2修复(2026-10-28悬空槽案): temporal_reuse.comp:64经FetchDepthTex读DEPTH[0](本帧
            // 深度做时域拒绝),原只绑[1](Previous Depth)为索引错位——全屏每帧读悬空槽=GI时域噪声
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 1, builder.GetTexture("Previous Depth"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_SURFACE_CACHE, 0, builder.GetTexture("Surface Cache Diffuse/Metallic"))
            .Read(2, SET2_BINDING_SURFACE_CACHE, 1, builder.GetTexture("Surface Cache Normal/Roughness"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_SURFACE_CACHE, 2, builder.GetTexture("Surface Cache Emission"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_SURFACE_CACHE, 3, builder.GetTexture("Surface Cache Lighting"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_SURFACE_CACHE, 4, builder.GetTexture("Surface Cache Depth"), VIEW_TYPE_2D, {})
            .ReadWrite(2, SET2_BINDING_RESTIR_GI_RESERVOIRS, 0, resBuffer0)
            .ReadWrite(2, SET2_BINDING_RESTIR_GI_PREV_RESERVOIRS, 0, reservoirA, 0, 0)
            .Read(2, SET2_BINDING_GBUFFER_EMISSION, 0, emission)                  // S4终态边复原(2026-10-27终裁)：temporal_reuse.comp:59 texelFetch；S5回退误删(10-21刀7b验收时在场,shader自10-21未变)
            .Read(2, SET2_BINDING_REPROJECTION_RESULT, 0, reprojectionOut)        // 复原：temporal_reuse.comp:117 texelFetch
            .ReadWrite(2, SET2_BINDING_RESTIR_GI_SETTING, 0, settingBuf, 0, 0)   // 复原：temporal_reuse.comp:38/78/115 只读SSBO(随DI/GI全族RW边形态,照S5快照转换形态)
            .Execute([&](RDGPassContext context) {

                RHICommandListRef command = context.command;
                command->SetComputePipeline(computePipeline[0]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);  
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width / WIDTH_DOWNSAMPLE_RATE, 16),     
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height / HEIGHT_DOWNSAMPLE_RATE, 16), 
                                    1);
            })
            .Finish();

        RDGComputePassHandle pass1 = builder.CreateComputePass(GetName() + " Spatial Reuse")
            .PassIndex(currentIndex)
            .RootSignature(rootSignature)
            .Shaders({computeShader[1].shader})           // 刀7b-3：严格边校验
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))     // GPU-AV实锤：spatial reuse采b12:0但无边（悬空槽），2026-10-19补
            .Read(2, SET2_BINDING_GBUFFER_DIFFUSE_METALLIC, 0, diffuse)
            .Read(2, SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS, 0, normal)
            .Read(2, SET2_BINDING_GBUFFER_EMISSION, 0, emission)
            .ReadWrite(2, SET2_BINDING_RESTIR_GI_SETTING, 0, settingBuf, 0, 0)
            .ReadWrite(2, SET2_BINDING_RESTIR_GI_RESERVOIRS, 0, resBuffer0, 0, 0)
            .ReadWrite(2, SET2_BINDING_RESTIR_GI_RESULT_RESERVOIRS, 0, reservoirB, 0, 0)
            .ReadAccelerationStructure(builder.GetBuffer("TLAS Storage"))
            .Execute([&](RDGPassContext context) {

                RHICommandListRef command = context.command;
                command->SetComputePipeline(computePipeline[1]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);  
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width / WIDTH_DOWNSAMPLE_RATE, 16),     
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height / HEIGHT_DOWNSAMPLE_RATE, 16), 
                                    1);
            })
            .Finish();

        RDGComputePassHandle pass2 = builder.CreateComputePass(GetName() + " Lighting")
            .PassIndex(currentIndex)
            .RootSignature(rootSignature)
            .Shaders({computeShader[2].shader})           // 刀7b-3：严格边校验
            .Read(2, SET2_BINDING_GBUFFER_DIFFUSE_METALLIC, 0, diffuse)
            .Read(2, SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS, 0, normal)
            .Read(2, SET2_BINDING_GBUFFER_EMISSION, 0, emission)
            .Read(2, SET2_BINDING_GBUFFER_SPECULAR_F0_ROUGHNESS, 0, specularF0)
            .ReadWrite(2, SET2_BINDING_RESTIR_GI_SETTING, 0, settingBuf, 0, 0)
            .ReadWrite(2, SET2_BINDING_RESTIR_GI_RESULT_RESERVOIRS, 0, reservoirB, 0, 0)
            .ReadWrite(2, SET2_BINDING_FINAL_COLOR, 0, outColor)
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 1, builder.GetTexture("Previous Depth"), VIEW_TYPE_2D, {})
            .ReadWrite(2, SET2_BINDING_GI_DIFFUSE_COLOR, 0, restirDiffuseColor)
            .ReadWrite(2, SET2_BINDING_GI_SPECULAR_COLOR, 0, restirSpecularColor)
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
            .OutputReadWrite(resBuffer0)
            .OutputReadWrite(resBuffer1)
            .OutputReadWrite(resBuffer2)
            .Finish();
    }
}
