#include "PathTracingPass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <cstdint>

void PathTracingPass::Init()
{
    auto backend = EngineContext::RHI();

    rayGenShader     = Shader(EngineContext::File()->ShaderPath() + "path_tracing/raygen.rgen.spv", SHADER_FREQUENCY_RAY_GEN);
    rayMissShader    = Shader(EngineContext::File()->ShaderPath() + "path_tracing/miss.rmiss.spv", SHADER_FREQUENCY_RAY_MISS);
    closestHitShader = Shader(EngineContext::File()->ShaderPath() + "path_tracing/closesthit.rchit.spv", SHADER_FREQUENCY_CLOSEST_HIT);

    RHIShaderBindingTableInfo sbtInfo = {};
    sbtInfo.AddRayGenGroup(rayGenShader.shader);
    sbtInfo.AddHitGroup(closestHitShader.shader, nullptr, nullptr);
    sbtInfo.AddMissGroup(rayMissShader.shader);
    RHIShaderBindingTableRef sbt = EngineContext::RHI()->CreateShaderBindingTable(sbtInfo);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry({1, 0, 1, SHADER_FREQUENCY_RAY_TRACING, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 1, 1, SHADER_FREQUENCY_RAY_TRACING, RESOURCE_TYPE_RW_TEXTURE})
                     .AddPushConstant({128, SHADER_FREQUENCY_RAY_TRACING});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIRayTracingPipelineInfo pipelineInfo  = {};
    pipelineInfo.rootSignature              = rootSignature;
    pipelineInfo.shaderBindingTable         = sbt;
    rayTracingPipeline   = backend->CreateRayTracingPipeline(pipelineInfo);

    Extent2D extent = EngineContext::Render()->GetWindowsExtent();
    historyColorTex = EngineContext::RHI()->CreateTexture({
        .format = FORMAT_R32G32B32A32_SFLOAT,
        .extent = {extent.width, extent.height, 1},
        .arrayLayers = 1,
        .mipLevels = 1,
        .memoryUsage = MEMORY_USAGE_GPU_ONLY,
        .type = RESOURCE_TYPE_RW_TEXTURE | RESOURCE_TYPE_TEXTURE
    });
}   

void PathTracingPass::Build(RDGBuilder& builder) 
{
    if(IsEnabled())
    {
        RDGTextureHandle outColor = builder.GetTexture("Mesh Pass Out Color");

        RDGTextureHandle historyColor = builder .CreateTexture("Path Tracing History Color")
            .Import(historyColorTex, RESOURCE_STATE_UNDEFINED)
            .Finish();

        RDGRayTracingPassHandle pass = builder.CreateRayTracingPass(GetName())
        .RootSignature(rootSignature)
        .ReadWrite(1, 0, 0, outColor)
        .ReadWrite(1, 1, 0, historyColor)
        .Execute([&](RDGPassContext context) {       

            auto camera = EngineContext::World()->GetActiveScene()->GetActiveCamera();
            if( camera->IsMoved() || 
                EngineContext::Input()->KeyIsPressed(KEY_TYPE_R)) 
                setting.totalNumSamples = 0;  // 相机移动后/手动更新后重新累计
            setting.totalNumSamples += setting.numSamples;

            RHICommandListRef command = context.command; 
            command->SetRayTracingPipeline(rayTracingPipeline);
            command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);  
            command->BindDescriptorSet(context.descriptors[1], 1);
            command->PushConstants(&setting, sizeof(PathTracingSetting), SHADER_FREQUENCY_RAY_TRACING);
            command->TraceRays(  EngineContext::Render()->GetWindowsExtent().width, 
                                EngineContext::Render()->GetWindowsExtent().height, 
                                1);
        })
        .Finish();
    }
}
