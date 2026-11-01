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
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo());       
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
        .AddFlag(RDGPassFlags::ForceGraphicsQueue)
        .RootSignature(rootSignature)
        .Shaders({computeShader.shader})            
        .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))
        .Read(2, SET2_BINDING_HISTORY_DEPTH, 1, builder.GetTexture("Previous Depth"), VIEW_TYPE_2D, {})
        .Read(2, SET2_BINDING_HISTORY_VELOCITY, 0, builder.GetTexture("G-Buffer Velocity"))
        .Read(2, SET2_BINDING_HISTORY_OBJECT_ID, 0, builder.GetTexture("G-Buffer Object ID"))  
        .Read(2, SET2_BINDING_HISTORY_OBJECT_ID, 1, builder.GetTexture("History G-Buffer Object ID"))
        .Read(2, SET2_BINDING_REPROJECTION_CURRENT_NORMAL, 0, normal)
        .Read(2, SET2_BINDING_REPROJECTION_HISTORY_NORMAL, 0, historyNormal)
        .ReadWrite(2, SET2_BINDING_REPROJECTION_RESULT_OUT, 0, reprojectionOut)
        .Execute([&](RDGPassContext context) {       

            RHICommandListRef command = context.command; 
            command->SetComputePipeline(computePipeline);
            command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
            command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);   
            command->BindDescriptorSet(context.descriptors[2], 2);
            command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                1);
        })
        .Finish();
}
