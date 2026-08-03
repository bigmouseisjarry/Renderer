#include "ClipmapPass.h"
#include "Core/Math/Math.h"
#include "Function/Global/Definations.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <cstdint>
#include <memory>

void ClipmapPass::Init()
{
    auto backend = EngineContext::RHI();

    rayGenShader     = Shader(EngineContext::File()->ShaderPath() + "vxgi/raygen.rgen.spv", SHADER_FREQUENCY_RAY_GEN);
    rayMissShader    = Shader(EngineContext::File()->ShaderPath() + "vxgi/miss.rmiss.spv", SHADER_FREQUENCY_RAY_MISS);
    closestHitShader = Shader(EngineContext::File()->ShaderPath() + "vxgi/closesthit.rchit.spv", SHADER_FREQUENCY_CLOSEST_HIT);

    RHIShaderBindingTableInfo sbtInfo = {};
    sbtInfo.AddRayGenGroup(rayGenShader.shader);
    sbtInfo.AddHitGroup(closestHitShader.shader, nullptr, nullptr);
    sbtInfo.AddMissGroup(rayMissShader.shader);
    RHIShaderBindingTableRef sbt = EngineContext::RHI()->CreateShaderBindingTable(sbtInfo);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry({1, 0, 1, SHADER_FREQUENCY_RAY_TRACING, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 1, 1, SHADER_FREQUENCY_RAY_TRACING, RESOURCE_TYPE_RW_BUFFER})
                     .AddPushConstant({128, SHADER_FREQUENCY_RAY_TRACING});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIRayTracingPipelineInfo pipelineInfo  = {};
    pipelineInfo.rootSignature              = rootSignature;
    pipelineInfo.shaderBindingTable         = sbt;
    rayTracingPipeline   = backend->CreateRayTracingPipeline(pipelineInfo);

    Extent3D extent = Extent3D( CLIPMAP_VOXEL_COUNT,    
                                CLIPMAP_VOXEL_COUNT * CLIPMAP_MIPLEVEL, 
                                CLIPMAP_VOXEL_COUNT * 6);
    clipmapTexture = EngineContext::RHI()->CreateTexture({                  // CLIPMAP_VOXEL_COUNT设成128就是480MB 超级大
        .format = FORMAT_R16G16B16A16_SFLOAT,
        .extent = {extent.width, extent.height, extent.depth},
        .arrayLayers = 1,
        .mipLevels = 1,
        .memoryUsage = MEMORY_USAGE_GPU_ONLY,
        .type = RESOURCE_TYPE_RW_TEXTURE | RESOURCE_TYPE_TEXTURE
    });

    clipmap = std::make_shared<Clipmap>(CLIPMAP_MIPLEVEL, CLIPMAP_VOXEL_COUNT, CLIPMAP_MIN_VOXEL_SIZE);
}   

void ClipmapPass::Build(RDGBuilder& builder) 
{
    if(attachToCamera) targetPos = EngineContext::World()->GetActiveScene()->GetActiveCamera()->GetPosition();
    else
    {
        float speed = 5.0f;
        float delta = speed * EngineContext::GetDeltaTime() / 1000.0f;

        if(EngineContext::Input()->KeyIsPressed(KEY_TYPE_I)) targetPos += Vec3(1.0, 0.0, 0.0) * delta;
        if(EngineContext::Input()->KeyIsPressed(KEY_TYPE_K)) targetPos += Vec3(-1.0, 0.0, 0.0) * delta;
        if(EngineContext::Input()->KeyIsPressed(KEY_TYPE_J)) targetPos += Vec3(0.0, 0.0, -1.0) * delta;
        if(EngineContext::Input()->KeyIsPressed(KEY_TYPE_L)) targetPos += Vec3(0.0, 0.0, 1.0) * delta;
        if(EngineContext::Input()->KeyIsPressed(KEY_TYPE_U)) targetPos += Vec3(0.0, 1.0, 0.0) * delta;
        if(EngineContext::Input()->KeyIsPressed(KEY_TYPE_O)) targetPos += Vec3(0.0, -1.0, 0.0) * delta;
    };

    updateRegions = clipmap->Update(targetPos);
    // updateRegions = clipmap->Update(Vec3(0, 0, 0));
    if(fullUpdate || EngineContext::Input()->KeyIsPressed(KEY_TYPE_R))    // 手动全量更新
    {
        fullUpdate = false;
        updateRegions.clear();
        for(int i = 0; i < clipmap->LevelCount(); i++) updateRegions.push_back(clipmap->GetRegion(i));
    }

    clipmapInfoBuffer[EngineContext::ThreadPool()->ThreadFrameIndex()].SetData(clipmap->GetInfo());
    RDGBufferHandle clipmapBuf = builder.CreateBuffer("VXGI Clipmap Buffer")
        .Import(clipmapInfoBuffer[EngineContext::ThreadPool()->ThreadFrameIndex()].buffer, RESOURCE_STATE_UNDEFINED)
        .Finish();

    RDGTextureHandle clipmapTex = builder.CreateTexture("VXGI Clipmap Texture")
        .Import(clipmapTexture, RESOURCE_STATE_UNDEFINED)
        .Finish();

    if(IsEnabled())
    {
        // SetEnable(false);   // 一次性

        for(int i = 0; i < updateRegions.size(); i++)
        {
            std::string index = " [" + std::to_string(i) + "]";

            RDGRayTracingPassHandle pass = builder.CreateRayTracingPass(GetName() + index)
                .PassIndex(i)
                .RootSignature(rootSignature)
                .ReadWrite(1, 0, 0, clipmapTex, VIEW_TYPE_3D)
                .ReadWrite(1, 1, 0, clipmapBuf)
                .Execute([&](RDGPassContext context) {       

                    auto region = updateRegions[context.passIndex[0]];
                    VXGISetting setting = {};
                    setting.region = region;

                    RHICommandListRef command = context.command; 
                    command->SetRayTracingPipeline(rayTracingPipeline);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);  
                    command->BindDescriptorSet(context.descriptors[1], 1);
                    command->PushConstants(&setting, sizeof(VXGISetting), SHADER_FREQUENCY_RAY_TRACING);
                    command->TraceRays( region.extent.x(), 
                                        region.extent.y(), 
                                        region.extent.z());
                })
                .Finish();
        }
    }
}
