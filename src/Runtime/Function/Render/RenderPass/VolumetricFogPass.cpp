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
                     .AddEntry({1, 0, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})   //未计算积分时的体素纹理
                     .AddEntry({1, 1, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})   //未计算积分时的历史帧
                     .AddEntry({1, 2, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})   //积分体素纹理
                     .AddEntry({1, 3, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})   //输出纹理
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

        RDGTextureHandle voxelRawHistory = builder.CreateTexture("Volumetric Fog Voxel Raw History")
            .Import(historyColorTex->texture, RESOURCE_STATE_UNDEFINED)
            .Finish(); 

        RDGTextureHandle voxelIntegral = builder.CreateTexture("Volumetric Fog Voxel Integral") 
            .Exetent({ VOLUMETRIC_FOG_SIZE_X, VOLUMETRIC_FOG_SIZE_Y, VOLUMETRIC_FOG_SIZE_Z })
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();    

        RDGTextureHandle outColor = builder.GetTexture("Mesh Pass Out Color");    

        RDGComputePassHandle pass0 = builder.CreateComputePass(GetName() + " Light Injection")
            .RootSignature(rootSignature)
            .ReadWrite(1, 0, 0, voxelRaw, VIEW_TYPE_3D)
            .ReadWrite(1, 1, 0, voxelRawHistory, VIEW_TYPE_3D)
            .ReadWrite(1, 2, 0, voxelIntegral, VIEW_TYPE_3D)
            .ReadWrite(1, 3, 0, outColor)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[0]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(context.descriptors[1], 1);
                command->PushConstants(&setting, sizeof(VolumetricFogSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  groupCountX, 
                                    groupCountY, 
                                    groupCountZ);
            })
            .Finish();

        RDGComputePassHandle pass1 = builder.CreateComputePass(GetName() + " Integral")
            .RootSignature(rootSignature)
            .ReadWrite(1, 0, 0, voxelRaw, VIEW_TYPE_3D)
            .ReadWrite(1, 1, 0, voxelRawHistory, VIEW_TYPE_3D)
            .ReadWrite(1, 2, 0, voxelIntegral, VIEW_TYPE_3D)
            .ReadWrite(1, 3, 0, outColor)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[1]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(context.descriptors[1], 1);
                command->PushConstants(&setting, sizeof(VolumetricFogSetting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  groupCountX, 
                                    groupCountY, 
                                    1);
            })
            .Finish();

        RDGComputePassHandle pass2 = builder.CreateComputePass(GetName() + " Combine")
            .RootSignature(rootSignature)
            .ReadWrite(1, 0, 0, voxelRaw, VIEW_TYPE_3D)
            .ReadWrite(1, 1, 0, voxelRawHistory, VIEW_TYPE_3D)
            .ReadWrite(1, 2, 0, voxelIntegral, VIEW_TYPE_3D)
            .ReadWrite(1, 3, 0, outColor)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[2]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(context.descriptors[1], 1);
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
