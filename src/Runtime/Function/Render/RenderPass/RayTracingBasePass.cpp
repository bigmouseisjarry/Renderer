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
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo())       // 刀7b-3：set2全局统一RS（98槽）
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
        RDGTextureHandle outColor = builder.GetTexture("Noisy HDR Color");

        // 刀5：mesh card readback经SC域b27（FetchSurfaceCacheLighting写时间戳；与PostProcessing共享节点）
        RDGBufferHandle cardUpdate = builder.GetOrCreateBuffer("Surface Cache Card Update")
            .Import(EngineContext::RenderResource()->GetCardUpdateBuffer(), RESOURCE_STATE_UNORDERED_ACCESS)
            .Finish();

        RDGRayTracingPassHandle pass = builder.CreateRayTracingPass(GetName())
            .RootSignature(rootSignature)
            .Shaders({rayGenShader.shader, rayMissShader.shader, closestHitShader.shader})   // 刀7b-3：严格边校验
            .ReadWrite(2, SET2_BINDING_MESH_CARD_READBACK, 0, cardUpdate)
            .Read(2, SET2_BINDING_SURFACE_CACHE, 0, builder.GetTexture("Surface Cache Diffuse/Metallic"))
            .Read(2, SET2_BINDING_SURFACE_CACHE, 1, builder.GetTexture("Surface Cache Normal/Roughness"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_SURFACE_CACHE, 2, builder.GetTexture("Surface Cache Emission"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_SURFACE_CACHE, 3, builder.GetTexture("Surface Cache Lighting"), VIEW_TYPE_2D, {})
            .Read(2, SET2_BINDING_SURFACE_CACHE, 4, builder.GetTexture("Surface Cache Depth"), VIEW_TYPE_2D, {})
            .ReadAccelerationStructure(builder.GetBuffer("TLAS Storage"))      // 依赖TLAS Update的产出（虚拟边，不绑定描述符；RT读AS）
            .ReadWrite(2, SET2_BINDING_RTB_A, 0, outColor)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetRayTracingPipeline(rayTracingPipeline);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);  
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->PushConstants(&setting, sizeof(RayTracingBaseSetting), SHADER_FREQUENCY_RAY_TRACING);
                command->TraceRays(  EngineContext::Render()->GetWindowsExtent().width, 
                                    EngineContext::Render()->GetWindowsExtent().height, 
                                    1);
            })
            .Finish();
    }
}
