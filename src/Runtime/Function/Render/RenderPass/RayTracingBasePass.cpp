#include "RayTracingBasePass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <cstdint>

void RayTracingBasePass::Init()
{
    auto backend = EngineContext::RHI();

    rayGenShader     = Shader(EngineContext::File()->ShaderPath() + "ray_tracing/raygen.rgen.spv", SHADER_FREQUENCY_RAY_GEN);
    rayMissShader    = Shader(EngineContext::File()->ShaderPath() + "ray_tracing/miss.rmiss.spv", SHADER_FREQUENCY_RAY_MISS);
    closestHitShader = Shader(EngineContext::File()->ShaderPath() + "ray_tracing/closesthit.rchit.spv", SHADER_FREQUENCY_CLOSEST_HIT);

    RHIShaderBindingTableInfo sbtInfo = {};
    sbtInfo.AddRayGenGroup(rayGenShader.shader);
    sbtInfo.AddHitGroup(closestHitShader.shader, nullptr, nullptr);
    sbtInfo.AddMissGroup(rayMissShader.shader);
    RHIShaderBindingTableRef sbt = EngineContext::RHI()->CreateShaderBindingTable(sbtInfo);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry({1, 0, 1, SHADER_FREQUENCY_RAY_TRACING, RESOURCE_TYPE_RW_TEXTURE})
                     .AddPushConstant({128, SHADER_FREQUENCY_RAY_TRACING});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIRayTracingPipelineInfo pipelineInfo  = {};
    pipelineInfo.rootSignature              = rootSignature;
    pipelineInfo.shaderBindingTable         = sbt;
    rayTracingPipeline   = backend->CreateRayTracingPipeline(pipelineInfo);
}   

void RayTracingBasePass::Build(RDGBuilder& builder) 
{
    if(IsEnabled())
    {
        RDGTextureHandle outColor = builder.GetTexture("Mesh Pass Out Color");

        RDGRayTracingPassHandle pass = builder.CreateRayTracingPass(GetName())
        .RootSignature(rootSignature)
        .ReadWrite(1, 0, 0, outColor)
        .Execute([&](RDGPassContext context) {       

            RHICommandListRef command = context.command; 
            command->SetRayTracingPipeline(rayTracingPipeline);
            command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);  
            command->BindDescriptorSet(context.descriptors[1], 1);
            command->PushConstants(&setting, sizeof(RayTracingBaseSetting), SHADER_FREQUENCY_RAY_TRACING);
            command->TraceRays(  EngineContext::Render()->GetWindowsExtent().width, 
                                EngineContext::Render()->GetWindowsExtent().height, 
                                1);
        })
        .Finish();
    }
}
