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
                     .AddEntry({1, 0, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})   // SSSR_HIT_RESULT
                     .AddEntry({1, 1, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})   // SSSR_HIT_COLOR
                     .AddEntry({1, 2, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})   // SSSR_PDF
                     .AddEntry({1, 3, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})   // SSSR_RESOLVE
                     .AddEntry({1, 4, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})   // SSSR_HISTORY
                     .AddEntry({1, 5, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})   // OUT_COLOR
                     .AddEntry({1, 6, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})      // IN_COLOR_PYRAMID
                     .AddEntry({1, 7, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})      // BRDF_LUT
                     .AddEntry({1, 8, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})      // REPROJECTION_RESULT
                     .AddEntry({2, 0, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })     // G_BUFFER_DIFFUSE_METALLIC
                     .AddEntry({2, 1, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })     // G_BUFFER_NORMAL_ROUGHNESS
                     .AddEntry({2, 2, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })     // G_BUFFER_EMISSION
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
        Extent2D halfWindowExtent = HALF_SIZE_SSSR ? EngineContext::Render()->GetHalfWindowsExtent() : windowExtent;

        RDGTextureHandle sssrHit = builder.CreateTexture("SSSR Hit Result")
            .Exetent({halfWindowExtent.width, halfWindowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();

        RDGTextureHandle sssrColor = builder.CreateTexture("SSSR Hit Color")
            .Exetent({halfWindowExtent.width, halfWindowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();

        RDGTextureHandle sssrPDF = builder.CreateTexture("SSSR PDF")
            .Exetent({halfWindowExtent.width, halfWindowExtent.height, 1})
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

        RDGTextureHandle sssrHistory = builder.CreateTexture("SSSR History")
            .Import(historyColorTex->texture, RESOURCE_STATE_UNDEFINED)
            .Finish();

        RDGTextureHandle outColor = builder.GetTexture("Mesh Pass Out Color");

        // 第2刀：SSSR改采上帧最终颜色。pyramid源从"Mesh Pass Out Color"（本帧Deferred Lighting产物，
        // 迫使整条SSSR链排在GI链后）换成持久import的上帧颜色（TAA每帧末kernel直写ping-pong对）。
        // pyramid得以在帧首生成，Trace/Resolve依赖面收窄到G-Buffer+Reprojection；SSR滞后一帧为业界
        // 标准做法（FidelityFX SSSR/ReSTIR同），disocclusion由SSSR History既有机制兜底。
        // 本pass先于TAA Build：此处GetOrCreate负责创建节点，TAA侧同名Import断言幂等（同纹理同state）。
        uint32_t historyIndex = EngineContext::CurrentFrameIndex();
        RDGTextureHandle lastFrameColor = builder.GetOrCreateTexture("Final Color History")
            .Import(EngineContext::RenderResource()->GetFinalColorHistoryTexture(historyIndex), RESOURCE_STATE_UNDEFINED)
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

        // 帧首即生成上帧颜色mip（copy源为持久import纹理，无帧内生产者依赖）
        RDGCopyPassHandle copyPass = builder.CreateCopyPass(GetName() + " Color Pyramid")
            .From(lastFrameColor)
            .To(inColorPyramid)
            .GenerateMips()
            .Finish();

        // 第3刀·动作2：SSSR链钉在graphics队列——利用q0在G-Buffer后~11ms的空窗期与q1的GI链真并行
        // （依赖面已收窄到Reproj+G-Buffer+帧首pyramid，两条链独立；classify见queue_schedule的ForceGraphicsQueue分支）
        // OUT_COLOR(1,5)在trace/resolve shader中零访问（仅combine/filter写），不再声明——
        // 消除对Deferred Lighting产物的假依赖边，Trace/Resolve得以与DefLight链并行
        RDGComputePassHandle pass0 = builder.CreateComputePass(GetName() + " Trace")
            .AddFlag(RDGPassFlags::ForceGraphicsQueue)
            .RootSignature(rootSignature)
            .ReadWrite(1, 0, 0, sssrHit)
            .ReadWrite(1, 1, 0, sssrColor)
            .ReadWrite(1, 2, 0, sssrPDF)
            .ReadWrite(1, 3, 0, sssrResolve)
            .ReadWrite(1, 4, 0, sssrHistory)
            .Read(1, 6, 0, inColorPyramid)
            .Read(1, 7, 0, brdfLut)
            .Read(1, 8, 0, reprojectionOut)
            .Read(2, 0, 0, diffuse)
            .Read(2, 1, 0, normal)
            .Read(2, 2, 0, emission)
            // 隐形依赖边（图外通道消费）：Trace的RayTraceSurfaceCache做TLAS ray query +
            // FetchSurfaceCacheLighting采样表面缓存（均经per-frame set）
            .Dependency(builder.GetBuffer("TLAS Storage"), RESOURCE_STATE_ACCELERATION_STRUCTURE)
            .Dependency(builder.GetTexture("Surface Cache Lighting"), RESOURCE_STATE_SHADER_RESOURCE)
            .Execute([&](RDGPassContext context) {       

                Extent2D extent = (HALF_SIZE_SSSR) ? EngineContext::Render()->GetHalfWindowsExtent() : 
                                                     EngineContext::Render()->GetWindowsExtent();

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[0]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);  
                command->BindDescriptorSet(context.descriptors[1], 1);
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
            .ReadWrite(1, 0, 0, sssrHit)
            .ReadWrite(1, 1, 0, sssrColor)
            .ReadWrite(1, 2, 0, sssrPDF)
            .ReadWrite(1, 3, 0, sssrResolve)
            .ReadWrite(1, 4, 0, sssrHistory)
            .Read(1, 6, 0, inColorPyramid)
            .Read(1, 7, 0, brdfLut)
            .Read(1, 8, 0, reprojectionOut)
            .Read(2, 0, 0, diffuse)
            .Read(2, 1, 0, normal)
            .Read(2, 2, 0, emission)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[1]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);  
                command->BindDescriptorSet(context.descriptors[1], 1);
                command->BindDescriptorSet(context.descriptors[2], 2); 
                command->PushConstants(&setting, sizeof(SSSRSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();

        if(!EngineContext::Render()->IsPassEnabled(NRD_PASS))    // 有NRD就不在这里做filer和合并了
        {
            // Filter同Combine理由留q1（无NRD路径下它承担合成，依赖面相同）
            RDGComputePassHandle pass2 = builder.CreateComputePass(GetName() + " Filter")
                .RootSignature(rootSignature)
                .ReadWrite(1, 0, 0, sssrHit)
                .ReadWrite(1, 1, 0, sssrColor)
                .ReadWrite(1, 2, 0, sssrPDF)
                .ReadWrite(1, 3, 0, sssrResolve)
                .ReadWrite(1, 4, 0, sssrHistory)
                .ReadWrite(1, 5, 0, outColor)
                .Read(1, 6, 0, inColorPyramid)
                .Read(1, 7, 0, brdfLut)
                .Read(1, 8, 0, reprojectionOut)      
                .Read(2, 0, 0, diffuse)
                .Read(2, 1, 0, normal)
                .Read(2, 2, 0, emission)
                .Execute([&](RDGPassContext context) {       

                    RHICommandListRef command = context.command; 
                    command->SetComputePipeline(computePipeline[2]);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);  
                    command->BindDescriptorSet(context.descriptors[1], 1);
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
            // Combine不钉q0：其outColor依赖锚在q1的DefLight上（DefLight→SSSR Combine→NRD Combine→Forward
            // 是读改写链），钉q0会把DefLight批末等待折到所在批头、连累Trace/Resolve一起等；
            // 留q1则outColor全链队内自闭，q0的Trace→NRD Spec链零跨族等待
            RDGComputePassHandle pass3 = builder.CreateComputePass(GetName() + " Combine")
                .RootSignature(rootSignature)
                .ReadWrite(1, 3, 0, sssrResolve)
                .ReadWrite(1, 5, 0, outColor)
                .Read(1, 6, 0, inColorPyramid)
                .Execute([&](RDGPassContext context) {       

                    RHICommandListRef command = context.command; 
                    command->SetComputePipeline(computePipeline[3]);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);  
                    command->BindDescriptorSet(context.descriptors[1], 1);
                    command->PushConstants(&setting, sizeof(SSSRSetting), SHADER_FREQUENCY_COMPUTE);
                    command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                        Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                        1);
                })
                .Finish();
        }
    }
}
