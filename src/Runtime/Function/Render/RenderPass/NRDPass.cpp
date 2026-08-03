#include "NRDPass.h"
#include "Core/Math/Math.h"
#include "Function/Global/Definations.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RDG/RDGNode.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderPass/RenderPass.h"
#include "NRDDescs.h"
#include "NRDSettings.h"
#include <cstdint>
#include <cstring>

void NRDPass::Init()
{
    Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();
    auto backend = EngineContext::RHI();

    const nrd::DenoiserDesc denoiserDescs[] =
    {
        { 0, nrd::Denoiser::RELAX_DIFFUSE },
        { 1, nrd::Denoiser::RELAX_SPECULAR },
        { 2, nrd::Denoiser::RELAX_DIFFUSE_SPECULAR },
        { 3, nrd::Denoiser::REBLUR_DIFFUSE },
        { 4, nrd::Denoiser::REBLUR_SPECULAR },
        { 5, nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR },
    };

    nrd::InstanceCreationDesc instanceCreationDesc = {};
    instanceCreationDesc.denoisers = denoiserDescs;
    instanceCreationDesc.denoisersNum = 6;  //
        
    NRDIntegrationCreationDesc nrdIntegrationDesc = {};
    nrdIntegrationDesc.queuedFrameNum = FRAMES_IN_FLIGHT;      // i.e. number of frames "in-flight"
    nrdIntegrationDesc.resourceWidth = windowExtent.width;
    nrdIntegrationDesc.resourceHeight = windowExtent.height;
    nrdIntegrationDesc.demoteFloat32to16 = false;

    integration.Recreate(nrdIntegrationDesc, instanceCreationDesc);


    computeShader[0] = Shader(EngineContext::File()->ShaderPath() + "nrd/view_z.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[1] = Shader(EngineContext::File()->ShaderPath() + "nrd/nrd_combine.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[2] = Shader(EngineContext::File()->ShaderPath() + "nrd/copy_sssr.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry({1, 0, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE})
                     .AddEntry({1, 1, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 2, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 3, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 4, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 5, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})
                     .AddEntry({1, 6, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})
                     .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;
    pipelineInfo.computeShader              = computeShader[0].shader;
    computePipeline[0]   = backend->CreateComputePipeline(pipelineInfo);

    pipelineInfo.computeShader              = computeShader[1].shader;
    computePipeline[1]   = backend->CreateComputePipeline(pipelineInfo);

    pipelineInfo.computeShader              = computeShader[2].shader;
    computePipeline[2]   = backend->CreateComputePipeline(pipelineInfo);

    // confidenceTexture = EngineContext::RHI()->CreateTexture({
    //     .format = FORMAT_R16_SFLOAT,
    //     .extent = {windowExtent.width, windowExtent.height, 1},
    //     .arrayLayers = 1,
    //     .mipLevels = 1,
    //     .memoryUsage = MEMORY_USAGE_GPU_ONLY,
    //     .type = RESOURCE_TYPE_RW_TEXTURE | RESOURCE_TYPE_TEXTURE
    // });
}   

void NRDPass::Build(RDGBuilder& builder) 
{
    bool restirEnabled = EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) ||
                         EngineContext::Render()->IsPassEnabled(RESTIR_GI_PASS);

    bool diffuseEnabled = restirEnabled &&
                          !EngineContext::Render()->IsPassEnabled(SVGF_PASS);

    bool specularEnabled = EngineContext::Render()->IsPassEnabled(SSSR_PASS);

    if( IsEnabled() &&
        (diffuseEnabled || specularEnabled) &&
        !EngineContext::Render()->IsPassEnabled(PATH_TRACING_PASS) &&
        !EngineContext::Render()->IsPassEnabled(RAY_TRACING_BASE_PASS))
    {
        Extent2D windowExtent = EngineContext::Render()->GetWindowsExtent();
        auto camera = EngineContext::World()->GetActiveScene()->GetActiveCamera();  

        setting.denoisedOnly = (EngineContext::Render()->IsPassEnabled(RESTIR_DI_PASS) || denoisedOnly) ? 1 : 0;   
        setting.specularOnly = !diffuseEnabled ? 1 : 0;   
        setting.restirEnabled = restirEnabled ? 1 : 0;

        RDGTextureHandle diffuse                = builder.GetTexture("G-Buffer Diffuse/Metallic");
        RDGTextureHandle normal                 = builder.GetTexture("G-Buffer Normal/Roughness");
        RDGTextureHandle velocity               = builder.GetTexture("G-Buffer Velocity");
        RDGTextureHandle depth                  = builder.GetTexture("Depth");
        RDGTextureHandle outColor               = builder.GetTexture("Mesh Pass Out Color");

        RDGTextureHandle restirDiffuseColor = builder.GetOrCreateTexture("ReSTIR Diffuse Color") 
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .AllowRenderTarget()
            .Finish(); 

        RDGTextureHandle restirSpecularColor = builder.GetOrCreateTexture("ReSTIR Specular Color") 
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .AllowRenderTarget()
            .Finish();

        // RDGTextureHandle confidence = builder.CreateTexture("NRD Confidence")
        //     .Import(confidenceTexture, frameIndex == 0 ? RESOURCE_STATE_UNDEFINED : RESOURCE_STATE_SHADER_RESOURCE)
        //     .Finish();

        RDGTextureHandle nrdNormal = builder.CreateTexture("NRD Normal/Roughness")
            .Exetent({windowExtent.width, windowExtent.height, 1})
            //.Format(FORMAT_R8G8B8A8_UNORM)
            .Format(FORMAT_R8G8B8A8_SNORM)
            //.Format(FORMAT_A2R10G10B10_UNORM)
            //.Format(FORMAT_R16G16B16A16_UNORM)
            //.Format(FORMAT_R16G16B16A16_SNORM)
            //.Format(FORMAT_R16G16B16A16_SFLOAT)
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();

        RDGTextureHandle nrdViewZ = builder.CreateTexture("NRD View Z")
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(FORMAT_R32_SFLOAT)
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();  

        RDGTextureHandle nrdOutDiffuse = builder.CreateTexture("NRD Out Diffuse") 
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish(); 
            
        RDGTextureHandle nrdOutSpecular = builder.CreateTexture("NRD Out Specular") 
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish(); 

        RDGTextureHandle nrdOutDebug = builder.CreateTexture("NRD Out Debug")   // TODO
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(EngineContext::Render()->GetHdrColorFormat())
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish(); 

        if(specularEnabled)
        {
            RDGTextureHandle sssrResolve = builder.GetTexture("SSSR Resolve");

            RDGComputePassHandle pass2 = builder.CreateComputePass(GetName() + " Copy SSSR")
                .RootSignature(rootSignature)
                .ReadWrite(1, 1, 0, restirSpecularColor)
                .ReadWrite(1, 2, 0, sssrResolve)
                .Execute([&](RDGPassContext context) {       

                    RHICommandListRef command = context.command; 
                    command->SetComputePipeline(computePipeline[2]);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                    command->BindDescriptorSet(context.descriptors[1], 1);
                    command->PushConstants(&setting, sizeof(setting), SHADER_FREQUENCY_COMPUTE);
                    command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                        Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                        1);
                })
                .Finish();
        }

        RDGComputePassHandle pass = builder.CreateComputePass(GetName() + " Generate View Z")
            .RootSignature(rootSignature)
            .Read(1, 0, 0, depth, VIEW_TYPE_2D, {TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1})
            .ReadWrite(1, 1, 0, diffuse)
            .ReadWrite(1, 2, 0, normal)
            .ReadWrite(1, 3, 0, restirDiffuseColor)
            .ReadWrite(1, 4, 0, restirSpecularColor)
            .ReadWrite(1, 5, 0, nrdViewZ)
            .ReadWrite(1, 6, 0, nrdNormal)
            .Execute([&](RDGPassContext context) {       

                RHICommandListRef command = context.command; 
                command->SetComputePipeline(computePipeline[0]);
                command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                command->BindDescriptorSet(context.descriptors[1], 1);
                command->PushConstants(&setting, sizeof(setting), SHADER_FREQUENCY_COMPUTE);
                command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                    Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                    1);
            })
            .Finish();

        {
            integration.NewFrame();

            Mat4 view = camera->GetViewMatrix();    
            Mat4 proj = camera->GetProjectionMatrix();
            Mat4 prevView = camera->GetPrevViewMatrix();
            Mat4 prevProj = camera->GetPrevProjectionMatrix();
            
            commonSettings.frameIndex = frameIndex++;
            memcpy(commonSettings.viewToClipMatrixPrev, prevProj.array().data(), 16 * sizeof(float));
            memcpy(commonSettings.viewToClipMatrix, proj.array().data(), 16 * sizeof(float));
            memcpy(commonSettings.worldToViewMatrixPrev, prevView.array().data(), 16 * sizeof(float));
            memcpy(commonSettings.worldToViewMatrix, view.array().data(), 16 * sizeof(float));
            commonSettings.motionVectorScale[0] = setting.motionVectorScaleX;
            commonSettings.motionVectorScale[1] = setting.motionVectorScaleY;
            commonSettings.motionVectorScale[2] = 0.0f;
            //commonSettings.motionVectorScale[2] = setting.motionVectorScaleZ;
            commonSettings.isMotionVectorInWorldSpace = false;
            commonSettings.isHistoryConfidenceAvailable = false;
            commonSettings.isDisocclusionThresholdMixAvailable = false;
            commonSettings.isBaseColorMetalnessAvailable = commonSettings.isBaseColorMetalnessAvailable;   
            commonSettings.splitScreen = setting.splitScreen;
            commonSettings.resourceSize[0] = (uint16_t)windowExtent.width;
            commonSettings.resourceSize[1] = (uint16_t)windowExtent.height;
            commonSettings.resourceSizePrev[0] = (uint16_t)windowExtent.width;
            commonSettings.resourceSizePrev[1] = (uint16_t)windowExtent.height;
            commonSettings.rectSize[0] = (uint16_t)windowExtent.width;
            commonSettings.rectSize[1] = (uint16_t)windowExtent.height;
            commonSettings.rectSizePrev[0] =(uint16_t)windowExtent.width;
            commonSettings.rectSizePrev[1] = (uint16_t)windowExtent.height;
            commonSettings.viewZScale = 1.0f;
            commonSettings.enableValidation = debug; // debug模式   
            integration.SetCommonSettings(commonSettings);
    
            relaxSettings.enableAntiFirefly = setting.enableAntiFirefly > 0;
            relaxSettings.checkerboardMode = nrd::CheckerboardMode::OFF;
            integration.SetDenoiserSettings(0, &relaxSettings);
            integration.SetDenoiserSettings(1, &relaxSettings);
            integration.SetDenoiserSettings(2, &relaxSettings);

            reblurSettings.hitDistanceParameters = {};
            reblurSettings.enableAntiFirefly = setting.enableAntiFirefly > 0;
            integration.SetDenoiserSettings(3, &reblurSettings);
            integration.SetDenoiserSettings(4, &reblurSettings);
            integration.SetDenoiserSettings(5, &reblurSettings);

            NRDResourceSnapshot resourceSnapshot = {};
            resourceSnapshot.SetResource(nrd::ResourceType::IN_NORMAL_ROUGHNESS, nrdNormal);
            resourceSnapshot.SetResource(nrd::ResourceType::IN_MV, velocity);
            resourceSnapshot.SetResource(nrd::ResourceType::IN_VIEWZ, nrdViewZ);
            resourceSnapshot.SetResource(nrd::ResourceType::IN_BASECOLOR_METALNESS, diffuse);
            resourceSnapshot.SetResource(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, restirDiffuseColor);
            resourceSnapshot.SetResource(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST, restirSpecularColor);
            resourceSnapshot.SetResource(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, nrdOutDiffuse);
            resourceSnapshot.SetResource(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST, nrdOutSpecular);
            resourceSnapshot.SetResource(nrd::ResourceType::OUT_VALIDATION, nrdOutDebug);

            //const nrd::Identifier denoisers[] = { setting.nrdMode };                  // 测试下来RELAX降噪ReSTIR GI的结果效果不好，
            const nrd::Identifier denoisers[] = { 1, 3 };                       // REBLUR的镜面反射在法线上有virtual history的bug?
            //const nrd::Identifier denoisers[] = { 1, 0 }; 
            integration.Denoise(builder, denoisers, setting.specularOnly ? 1 : 2, resourceSnapshot);
            //integration.Destroy();
        }

        if(!debug)
        {
            RDGComputePassHandle pass1 = builder.CreateComputePass(GetName() + " Combine")
                .RootSignature(rootSignature)
                .ReadWrite(1, 1, 0, diffuse)
                .ReadWrite(1, 2, 0, normal)
                .ReadWrite(1, 3, 0, nrdOutDiffuse)
                .ReadWrite(1, 4, 0, nrdOutSpecular)
                .ReadWrite(1, 5, 0, outColor)
                .Execute([&](RDGPassContext context) {       

                    RHICommandListRef command = context.command; 
                    command->SetComputePipeline(computePipeline[1]);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                    command->BindDescriptorSet(context.descriptors[1], 1);
                    command->PushConstants(&setting, sizeof(setting), SHADER_FREQUENCY_COMPUTE);
                    command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16), 
                                        Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16), 
                                        1);
                })
                .Finish();
        }
        else 
        {
            RDGCopyPassHandle copy = builder.CreateCopyPass(GetName() + "Debug")
                .From(nrdOutDebug)
                .To(outColor)
                .Finish();
        }
        
    }
    else 
	    frameIndex = 0;
}
