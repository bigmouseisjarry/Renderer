#include "VolumetricFogPass.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"

void VolumetricFogPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader[0] = Shader(EngineContext::File()->ShaderPath() + "volumetric_fog/volumetric_fog_light_injection.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[1] = Shader(EngineContext::File()->ShaderPath() + "volumetric_fog/volumetric_fog_integral.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[2] = Shader(EngineContext::File()->ShaderPath() + "volumetric_fog/volumetric_fog_screen.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetBindlessRootSignature()->GetInfo())
                     .AddEntry(EngineContext::RenderResource()->GetSet2RootSignature()->GetInfo())       // 刀7b-3：set2全局统一RS（98槽）
                     .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;
    
    for(int i = 0; i < 3; i++)
    {
        pipelineInfo.computeShader          = computeShader[i].shader; 
        computePipeline[i]   = backend->CreateComputePipeline(pipelineInfo);
    }


    historyColorTex = std::make_shared<Texture>( 
        TEXTURE_TYPE_3D, 
        EngineContext::Render()->GetHdrColorFormat(),
        Extent3D(VOLUMETRIC_FOG_SIZE_X, VOLUMETRIC_FOG_SIZE_Y, VOLUMETRIC_FOG_SIZE_Z),
        1, 1);  
}   

void VolumetricFogPass::Build(RDGBuilder& builder) 
{
    Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();

    if( IsEnabled() &&
        !EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) &&
        !EngineContext::Render()->IsPassEnabled(RAY_TRACING_BASE_PASS) && 
        !EngineContext::Render()->IsPassEnabled(PATH_TRACING_PASS))
    {

    RDGTextureHandle voxelRaw = builder.CreateTexture("Volumetric Fog Voxel Raw")
            .Exetent({ VOLUMETRIC_FOG_SIZE_X, VOLUMETRIC_FOG_SIZE_Y, VOLUMETRIC_FOG_SIZE_Z })
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();  

        // 稳态=UAV：末位触碰者是Combine的ReadWrite（本pass当前禁用，重启用时此声明已就位）
        RDGTextureHandle voxelRawHistory = builder.CreateTexture("Volumetric Fog Voxel Raw History")
            .Import(historyColorTex->texture)
            .Finish();

        RDGTextureHandle voxelIntegral = builder.CreateTexture("Volumetric Fog Voxel Integral") 
            .Exetent({ VOLUMETRIC_FOG_SIZE_X, VOLUMETRIC_FOG_SIZE_Y, VOLUMETRIC_FOG_SIZE_Z })
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();    

        RDGTextureHandle outColor = builder.GetTexture("Noisy HDR Color");    

        RDGComputePassHandle pass0 = builder.CreateComputePass(GetName() + " Light Injection")
            .RootSignature(rootSignature)
            .Shaders({computeShader[0].shader})          // 刀7b-3：严格边校验
            .ReadWrite(2, SET2_BINDING_LIGHT_CLUSTER_GRID, 0, builder.GetTexture("Light Cluster Grid"), VIEW_TYPE_3D)
            .ReadWrite(2, SET2_BINDING_LIGHT_CLUSTER_INDEX, 0, builder.GetBuffer("Light Cluster Index"))
            .ReadWrite(2, SET2_BINDING_FOG_VOXEL_CURRENT, 0, voxelRaw, VIEW_TYPE_3D)
            .ReadIfValid(2, SET2_BINDING_DIRECTIONAL_SHADOW, 0, "Directional Depth [0]", VIEW_TYPE_2D, { TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1})
            .ReadIfValid(2, SET2_BINDING_DIRECTIONAL_SHADOW, 1, "Directional Depth [1]", VIEW_TYPE_2D, { TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1 })
            .ReadIfValid(2, SET2_BINDING_DIRECTIONAL_SHADOW, 2, "Directional Depth [2]", VIEW_TYPE_2D, { TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1 })
            .ReadIfValid(2, SET2_BINDING_DIRECTIONAL_SHADOW, 3, "Directional Depth [3]", VIEW_TYPE_2D, { TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1 })
            .ReadIfValid(2, SET2_BINDING_POINT_SHADOW, 0, "Point Shadow Filtered Color [0]", VIEW_TYPE_CUBE, { TEXTURE_ASPECT_COLOR, 0, 1, 0, 6 })
            .ReadIfValid(2, SET2_BINDING_POINT_SHADOW, 1, "Point Shadow Filtered Color [1]", VIEW_TYPE_CUBE, { TEXTURE_ASPECT_COLOR, 0, 1, 0, 6 })
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[0]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->PushConstants(&setting, sizeof(VolumetricFogSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  groupCountX, 
                                    groupCountY, 
                                    groupCountZ);
            })
            .Finish();

        RDGComputePassHandle pass1 = builder.CreateComputePass(GetName() + " Integral")
            .RootSignature(rootSignature)
            .Shaders({computeShader[1].shader})          // 刀7b-3：严格边校验
            .ReadWrite(2, SET2_BINDING_FOG_VOXEL_CURRENT, 0, voxelRaw, VIEW_TYPE_3D)
            .ReadWrite(2, SET2_BINDING_FOG_VOXEL_HISTORY, 0, voxelRawHistory, VIEW_TYPE_3D)
            .ReadWrite(2, SET2_BINDING_FOG_VOXEL_INTEGRATED, 0, voxelIntegral, VIEW_TYPE_3D)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[1]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->PushConstants(&setting, sizeof(VolumetricFogSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  groupCountX, 
                                    groupCountY, 
                                    1);
            })
            .Finish();

        RDGComputePassHandle pass2 = builder.CreateComputePass(GetName() + " Combine")
            .RootSignature(rootSignature)
            .Shaders({computeShader[2].shader})          // 刀7b-3：严格边校验
            .Read(2, SET2_BINDING_HISTORY_DEPTH, 0, builder.GetTexture("Depth"))
            .ReadWrite(2, SET2_BINDING_FOG_VOXEL_INTEGRATED, 0, voxelIntegral, VIEW_TYPE_3D)
            .ReadWrite(2, SET2_BINDING_FOG_VOXEL_OUT, 0, outColor)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[2]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPoolDescriptorSet(), 1);
                command->BindDescriptorSet(context.descriptors[2], 2);
                command->PushConstants(&setting, sizeof(VolumetricFogSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();

        RDGCopyPassHandle copyPass = builder.CreateCopyPass(GetName() + " Copy History")
            .From(voxelRaw)
            .To(voxelRawHistory)
            .Finish();    
    }
}
