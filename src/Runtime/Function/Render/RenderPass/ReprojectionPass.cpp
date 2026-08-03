#include "ReprojectionPass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <cstdint>

void ReprojectionPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader = Shader(EngineContext::File()->ShaderPath() + "default/reprojection.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry({1, 0, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})
                     .AddEntry({1, 1, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})
                     .AddEntry({1, 2, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})
                     .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;
    pipelineInfo.computeShader              = computeShader.shader;
    computePipeline   = backend->CreateComputePipeline(pipelineInfo);
}   

void ReprojectionPass::Build(RDGBuilder& builder) 
{
    Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();
    
    RDGTextureHandle normal = builder.GetTexture("G-Buffer Normal/Roughness");
    RDGTextureHandle historyNormal = builder.GetTexture("History G-Buffer Normal/Roughness");

    RDGTextureHandle reprojectionOut = builder.CreateTexture("Reprojection Out")
        .Exetent({windowExtent.width, windowExtent.height, 1})
        .Format(FORMAT_R32G32B32A32_SFLOAT)
        .ArrayLayers(1)
        .MipLevels(1)
        .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
        .AllowReadWrite()
        .Finish();  

    RDGComputePassHandle pass = builder.CreateComputePass(GetName())
        .RootSignature(rootSignature)
        .Read(1, 0, 0, normal)
        .Read(1, 1, 0, historyNormal)
        .ReadWrite(1, 2, 0, reprojectionOut)
        .Execute([&](RDGPassContext context) {       

            RHICommandListRef command = context.command; 
            command->SetComputePipeline(computePipeline);
            command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);   
            command->BindDescriptorSet(context.descriptors[1], 1);
            command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                1);
        })
        .Finish();
}
