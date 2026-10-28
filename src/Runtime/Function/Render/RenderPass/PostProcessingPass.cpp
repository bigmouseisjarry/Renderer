#include "PostProcessingPass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <cstdint>

void PostProcessingPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader = Shader(EngineContext::File()->ShaderPath() + "post_process/post_process.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo())       // 刀7b-3：set2全局统一RS（98槽）
                     .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;
    pipelineInfo.computeShader              = computeShader.shader; 
    computePipeline   = backend->CreateComputePipeline(pipelineInfo);
}   

void PostProcessingPass::Build(RDGBuilder& builder) 
{

    setting.enable = IsEnabled() ? 1.0f : 0.0;

    Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

    RDGTextureHandle stableColor = builder.GetTexture("Stable HDR Color");

    RDGBufferHandle exposureData = builder.GetBuffer("Exposure Data");

    RDGTextureHandle outColor = builder.GetOrCreateTexture("Final LDR Color")
        .Exetent({windowExtent.width, windowExtent.height, 1})
        .Format(EngineContext::Render()->GetColorFormat())
        .ArrayLayers(1)
        .MipLevels(1)
        .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
        .AllowReadWrite()
        .AllowRenderTarget()
        .Finish();  

    RDGComputePassHandle pass = builder.CreateComputePass(GetName())
        .AddFlag(RDGPassFlags::ForceGraphicsQueue)
        .RootSignature(rootSignature)
        .Shaders({computeShader.shader})             // 刀7b-3：严格边校验
        .Read(2, SET2_BINDING_IN_COLOR, 0, stableColor)
        .ReadWrite(2, SET2_BINDING_POSTPROCESS_OUT, 0, outColor)
        .ReadWrite(2, SET2_BINDING_EXPOSURE_DATA, 0, exposureData, 0, 0)
        .Execute([&](RDGPassContext context) {       

            RHICommandListRef command = context.command; 
            command->SetComputePipeline(computePipeline);
            command->BindDescriptorSet(context.descriptors[2], 2);
            command->PushConstants(&setting, sizeof(PostProcessingSetting), SHADER_FREQUENCY_COMPUTE);
            command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                1);
        })
        .Finish();


    // TODO 换个位置
    // Surface Cache的更新回读
    
    // 刀5：改GetOrCreate——b27消费者（RTBase/Clipmap/SSSR/ReSTIRGI）先Build时会先建此节点，CreateBuffer不查黑板会造出同名双节点
    RDGBufferHandle cardUpdate = builder.GetOrCreateBuffer("Surface Cache Card Update")
        .Import(EngineContext::RenderResource()->GetCardUpdateBuffer(), RESOURCE_STATE_UNORDERED_ACCESS)
        .Finish();

    RDGBufferHandle cardReadBack = builder.CreateBuffer("Surface Cache Card Readback")
        .Import(EngineContext::RenderResource()->GetCardReadbackBuffer(), RESOURCE_STATE_UNDEFINED)
        .Finish();        

    RDGCopyPassHandle copy = builder.CreateCopyPass("Surface Cache Card Data")
        .From(cardUpdate, 0, sizeof(MeshCardReadBack))
        .To(cardReadBack, 0, sizeof(MeshCardReadBack))
        .OutputReadWrite(cardUpdate)
        .Finish();
}
