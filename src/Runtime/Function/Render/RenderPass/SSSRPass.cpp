#include "SSSRPass.h"
#include "Function/Global/Definations.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderPass/RenderPass.h"
#include <cstdint>

void SSSRPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader[0] = Shader(EngineContext::File()->ShaderPath() + "sssr/stochastic_ssr_trace.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[1] = Shader(EngineContext::File()->ShaderPath() + "sssr/stochastic_ssr_resolve.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[2] = Shader(EngineContext::File()->ShaderPath() + "sssr/stochastic_ssr_filter.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[3] = Shader(EngineContext::File()->ShaderPath() + "sssr/stochastic_ssr_combine.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo())       // 刀7b-3：set2全局统一RS（98槽）
                                                                                                                                                                                                                                                                                                                                                .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;
    for(int i = 0; i < 4; i++)
    {
        pipelineInfo.computeShader              = computeShader[i].shader;
        computePipeline[i]   = backend->CreateComputePipeline(pipelineInfo);
    }




    Extent2D extent = EngineContext::Render()->GetWindowsExtent();

    historyColorTex = std::make_shared<Texture>( 
        TEXTURE_TYPE_2D, 
        EngineContext::Render()->GetHdrColorFormat(),
        Extent3D(extent.width, extent.height, 1),
        1, 1);  
}   

void SSSRPass::Build(RDGBuilder& builder) 
{
    if( IsEnabled() && 
        //!EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) &&
        !EngineContext::Render()->IsPassEnabled(RAY_TRACING_BASE_PASS) && 
        !EngineContext::Render()->IsPassEnabled(PATH_TRACING_PASS))
    {
        Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();
        // Extent2D windowExtent = HALF_SIZE_SSSR ? EngineContext::Render()->GetHalfWindowsExtent() : windowExtent;

        RDGTextureHandle sssrHit = builder.CreateTexture("SSSR Hit Result")
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();

        RDGTextureHandle sssrColor = builder.CreateTexture("SSSR Hit Color")
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();

        RDGTextureHandle sssrPDF = builder.CreateTexture("SSSR PDF")
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();

        RDGTextureHandle sssrResolve = builder.CreateTexture("SSSR Resolve")
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();

        // 稳态=UAV：末位触碰者是Filter/Resolve对history的ReadWrite（无OutputRead收敛）
        RDGTextureHandle sssrHistory = builder.CreateTexture("SSSR History")
            .Import(historyColorTex->texture)
            .Finish();

        RDGTextureHandle outColor = builder.GetTexture("Noisy HDR Color");

        RDGTextureHandle lastFrameColor = builder.GetOrCreateTexture("Stable HDR Color History")
            .Import(EngineContext::RenderResource()->GetStableHDRColorTexture(1))     // 上一帧最终颜色（语义槽1）；与TAAPass同纹理同稳态
            .Finish();

        RDGTextureHandle inColorPyramid = builder.CreateTexture("SSSR Color Pyramid")
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(0)       // 自动生成全mip
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();

        RDGTextureHandle brdfLut = builder.GetTexture("BRDF LUT");

        RDGTextureHandle reprojectionOut = builder.GetTexture("Reprojection Out");

        RDGTextureHandle diffuse        = builder.GetTexture("G-Buffer Diffuse/Metallic");
        RDGTextureHandle normal         = builder.GetTexture("G-Buffer Normal/Roughness");
        RDGTextureHandle emission       = builder.GetTexture("G-Buffer Emission");
        RDGTextureHandle specularF0     = builder.GetTexture("G-Buffer Specular F0/Roughness");

        // 帧首即生成上帧颜色mip（copy源为持久import纹理，无帧内生产者依赖）
        RDGCopyPassHandle copyPass = builder.CreateCopyPass(GetName() + " Color Pyramid")
            .From(lastFrameColor)
            .To(inColorPyramid)
            .GenerateMips()
            .Finish();

        // 刀5：mesh card readback经SC域b27（FetchSurfaceCacheLighting写时间戳；与PostProcessing共享节点）
        RDGBufferHandle cardUpdate = builder.GetOrCreateBuffer("Surface Cache Card Update")
            .Import(EngineContext::RenderResource()->GetCardUpdateBuffer(), RESOURCE_STATE_UNORDERED_ACCESS)
            .Finish();

        RDGComputePassHandle pass0 = builder.CreateComputePass(GetName() + " Trace")
            .AddFlag(RDGPassFlags::ForceGraphicsQueue)
            .RootSignature(rootSignature)
            .Shaders({computeShader[0].shader})           // 刀7b-3：严格边校验
            .ReadWrite(2, SET2_BINDING_MESH_CARD_READBACK, 0, cardUpdate)
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))     // S4终态边复原(2026-10-27终裁)：trace.comp:131/151读DEPTH[0]本帧深度；S5回退误删(S5快照b2[0]绑Depth=S4边被当"S5加的"删)；同队q0读无QFO=S4稳态形态
            .ReadWrite(2, SET2_BINDING_SSSR_HIT_RESULT, 0, sssrHit)
            .Read(2, SET2_BINDING_HISTORY_DEPTH_PYRAMID, 0, builder.GetTexture("Depth Min"))
            .Read(2, SET2_BINDING_HISTORY_DEPTH_PYRAMID, 1, builder.GetTexture("Depth Max"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_SURFACE_CACHE, 0, builder.GetTexture("Surface Cache Diffuse/Metallic"))
            .Read(2, SET2_BINDING_SURFACE_CACHE, 1, builder.GetTexture("Surface Cache Normal/Roughness"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_SURFACE_CACHE, 2, builder.GetTexture("Surface Cache Emission"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_SURFACE_CACHE, 3, builder.GetTexture("Surface Cache Lighting"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_SURFACE_CACHE, 4, builder.GetTexture("Surface Cache Depth"), VIEW_TYPE_2D, {})
            .ReadWrite(2, SET2_BINDING_SSSR_HIT_COLOR, 0, sssrColor)
            .ReadWrite(2, SET2_BINDING_SSSR_PDF, 0, sssrPDF)
            .Read(2, SET2_BINDING_SSSR_IN_COLOR_PYRAMID, 0, inColorPyramid)
            .Read(2, SET2_BINDING_GBUFFER_DIFFUSE_METALLIC, 0, diffuse)
            .Read(2, SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS, 0, normal)
            .Read(2, SET2_BINDING_GBUFFER_SPECULAR_F0_ROUGHNESS, 0, specularF0)
            .ReadAccelerationStructure(builder.GetBuffer("TLAS Storage"))
            .Execute([&](RDGPassContext context) {       

                Extent2D extent = EngineContext::Render()->GetWindowsExtent();

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[0]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->PushConstants(&setting, sizeof(SSSRSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  Math::CeilDivide(extent.width, 16),
                                    Math::CeilDivide(extent.height, 16), 
                                    1);
            })
            .Finish();

        RDGComputePassHandle pass1 = builder.CreateComputePass(GetName() + " Resolve")
            .AddFlag(RDGPassFlags::ForceGraphicsQueue)
            .RootSignature(rootSignature)
            .Shaders({computeShader[1].shader})           // 刀7b-3：严格边校验
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))     // GPU-AV实锤：resolve采b12:0但无边（悬空槽→NaN光线家族），2026-10-19补
            .ReadWrite(2, SET2_BINDING_SSSR_HIT_RESULT, 0, sssrHit)
            .ReadWrite(2, SET2_BINDING_SSSR_HIT_COLOR, 0, sssrColor)
            .ReadWrite(2, SET2_BINDING_SSSR_PDF, 0, sssrPDF)
            .ReadWrite(2, SET2_BINDING_SSSR_RESOLVE, 0, sssrResolve)
            .Read(2, SET2_BINDING_SSSR_IN_COLOR_PYRAMID, 0, inColorPyramid)
            .Read(2, SET2_BINDING_BRDF_LUT, 0, brdfLut)
            .Read(2, SET2_BINDING_GBUFFER_DIFFUSE_METALLIC, 0, diffuse)
            .Read(2, SET2_BINDING_GBUFFER_NORMAL_ROUGHNESS, 0, normal)
            .Read(2, SET2_BINDING_GBUFFER_SPECULAR_F0_ROUGHNESS, 0, specularF0)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[1]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->PushConstants(&setting, sizeof(SSSRSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();

        if(!EngineContext::Render()->IsPassEnabled(NRD_PASS))    // 有NRD就不在这里做filer和合并了
        {
            RDGComputePassHandle pass2 = builder.CreateComputePass(GetName() + " Filter")
                .RootSignature(rootSignature)
                .Shaders({computeShader[2].shader})           // 刀7b-3：严格边校验
                .ReadWrite(2, SET2_BINDING_SSSR_HIT_RESULT, 0, sssrHit)
                .ReadWrite(2, SET2_BINDING_SSSR_RESOLVE, 0, sssrResolve)
                .ReadWrite(2, SET2_BINDING_SSSR_HISTORY, 0, sssrHistory)
                .ReadWrite(2, SET2_BINDING_OUT_COLOR, 0, outColor)
                .Read(2, SET2_BINDING_SSSR_REPROJECTION_RESULT, 0, reprojectionOut)      
                .Execute([&](RDGPassContext context) {       

                    RHICommandListRef command = context.command; 
                    command->SetComputePipeline(computePipeline[2]);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);  
                    command->BindDescriptorSet(context.descriptors[2], 2);
                    command->BindDescriptorSet(context.descriptors[2], 2); 
                    command->PushConstants(&setting, sizeof(SSSRSetting), SHADER_FREQUENCY_COMPUTE);
                    command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                        Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                        1);
                })
                .Finish();
        }
        else 
        {
            RDGComputePassHandle pass3 = builder.CreateComputePass(GetName() + " Combine")
                .RootSignature(rootSignature)
                .Shaders({computeShader[3].shader})           // 刀7b-3：严格边校验
                .ReadWrite(2, SET2_BINDING_SSSR_RESOLVE, 0, sssrResolve)
                .ReadWrite(2, SET2_BINDING_OUT_COLOR, 0, outColor)
                .Execute([&](RDGPassContext context) {       

                    RHICommandListRef command = context.command; 
                    command->SetComputePipeline(computePipeline[3]);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                    command->BindDescriptorSet(context.descriptors[2], 2);
                    command->BindDescriptorSet(context.descriptors[2], 2);
                    command->PushConstants(&setting, sizeof(SSSRSetting), SHADER_FREQUENCY_COMPUTE);
                    command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                        Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                        1);
                })
                .Finish();
        }
    }
}
