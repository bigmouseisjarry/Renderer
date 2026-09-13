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

    // 第3刀：NRD拆双实例——spec实例只注册RELAX_SPECULAR（SSSR用，跟SSSR链钉q0空窗），
    // diffuse实例只注册REBLUR_DIFFUSE（ReSTIR GI用，留q1）。原单实例注册全6个denoiser的模式废弃
    const nrd::DenoiserDesc specDenoiserDescs[] =
    {
        { 0, nrd::Denoiser::RELAX_SPECULAR },
    };
    const nrd::DenoiserDesc diffDenoiserDescs[] =
    {
        { 0, nrd::Denoiser::REBLUR_DIFFUSE },
    };

    nrd::InstanceCreationDesc specInstanceDesc = {};
    specInstanceDesc.denoisers = specDenoiserDescs;
    specInstanceDesc.denoisersNum = 1;

    nrd::InstanceCreationDesc diffInstanceDesc = {};
    diffInstanceDesc.denoisers = diffDenoiserDescs;
    diffInstanceDesc.denoisersNum = 1;

    NRDIntegrationCreationDesc nrdIntegrationDesc = {};
    nrdIntegrationDesc.queuedFrameNum = FRAMES_IN_FLIGHT;      // i.e. number of frames "in-flight"
    nrdIntegrationDesc.resourceWidth = windowExtent.width;
    nrdIntegrationDesc.resourceHeight = windowExtent.height;
    nrdIntegrationDesc.demoteFloat32to16 = false;

    NRDIntegrationCreationDesc specIntegrationDesc = nrdIntegrationDesc;
    strcpy(specIntegrationDesc.name, "NRD Spec");
    specIntegrationDesc.forceGraphicsQueue = true;             // spec链跟SSSR走graphics队列
    integrationSpec.Recreate(specIntegrationDesc, specInstanceDesc);

    NRDIntegrationCreationDesc diffIntegrationDesc = nrdIntegrationDesc;
    strcpy(diffIntegrationDesc.name, "NRD Diff");
    integrationDiff.Recreate(diffIntegrationDesc, diffInstanceDesc);


    computeShader[0] = Shader(EngineContext::File()->ShaderPath() + "nrd/view_z.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[1] = Shader(EngineContext::File()->ShaderPath() + "nrd/nrd_combine.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[2] = Shader(EngineContext::File()->ShaderPath() + "nrd/copy_sssr.comp.spv", SHADER_FREQUENCY_COMPUTE);

    {
        RHIRootSignatureInfo viewZRootSignatureInfo = {};
        viewZRootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
            .AddEntry({ 1, 0, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })      // G_BUFFER_DEPTH
            .AddEntry({ 1, 1, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })      // G_BUFFER_DIFFUSE_METALLIC
            .AddEntry({ 1, 2, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })      // G_BUFFER_NORMAL_ROUGHNESS
            .AddEntry({ 1, 3, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE })   // RESTIR_DIFFUSE_COLOR
            .AddEntry({ 1, 4, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE })   // RESTIR_SPECULAR_COLOR
            .AddEntry({ 1, 5, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE })   // NRD_VIEW_Z
            .AddEntry({ 1, 6, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE })  // NRD_NORMAL
            .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});   
                                                                 

        viewZRootSignature = backend->CreateRootSignature(viewZRootSignatureInfo);

        RHIComputePipelineInfo pipelineInfo = {};
        pipelineInfo.rootSignature = viewZRootSignature;
        pipelineInfo.computeShader = computeShader[0].shader;
        computePipeline[0] = backend->CreateComputePipeline(pipelineInfo);
    }


    {
        RHIRootSignatureInfo combineRootSignatureInfo = {};
        combineRootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
            .AddEntry({ 1, 1, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })      // G_BUFFER_DIFFUSE_METALLIC
            .AddEntry({ 1, 2, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })      // G_BUFFER_NORMAL_ROUGHNESS
            .AddEntry({ 1, 3, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })      // NRD_OUT_DIFFUSE
            .AddEntry({ 1, 4, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })      // NRD_OUT_SPECULAR
            .AddEntry({ 1, 5, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE })  // OUT
            .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});  

        combineRootSignature = backend->CreateRootSignature(combineRootSignatureInfo);

        RHIComputePipelineInfo pipelineInfo = {};
        pipelineInfo.rootSignature = combineRootSignature;
        pipelineInfo.computeShader = computeShader[1].shader;
        computePipeline[1] = backend->CreateComputePipeline(pipelineInfo);
    }

    {
        RHIRootSignatureInfo copySssrRootSignatureInfo = {};
        copySssrRootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
            .AddEntry({ 1, 1, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE })
            .AddEntry({ 1, 2, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE })
            .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});  

        copySssrRootSignature = backend->CreateRootSignature(copySssrRootSignatureInfo);

        RHIComputePipelineInfo pipelineInfo = {};
        pipelineInfo.rootSignature = copySssrRootSignature;
        pipelineInfo.computeShader = computeShader[2].shader;
        computePipeline[2] = backend->CreateComputePipeline(pipelineInfo);
    }

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

        // spec侧专用viewZ/normal/MV副本（与diff侧物理隔离——共享纹理的双族读者会让后取者
        // 等到先取者的最后一次读，拆开各用各的才真正并行）
        RDGTextureHandle nrdViewZSpec = builder.CreateTexture("NRD View Z Spec")
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(FORMAT_R32_SFLOAT)
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();

        RDGTextureHandle nrdNormalSpec = builder.CreateTexture("NRD Normal/Roughness Spec")
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(FORMAT_R8G8B8A8_SNORM)
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();

        RDGTextureHandle velocitySpec = builder.CreateTexture("NRD Velocity Spec")
            .Exetent({windowExtent.width, windowExtent.height, 1})
            .Format(FORMAT_R32G32_SFLOAT)
            .ArrayLayers(1)
            .MipLevels(1)
            .MemoryUsage(MEMORY_USAGE_GPU_ONLY)
            .AllowReadWrite()
            .Finish();

        NRDSetting specSetting = setting;  specSetting.side = 1;   // view_z仅打包specular输入
        NRDSetting diffSetting = setting;  diffSetting.side = 2;   // view_z仅打包diffuse输入

        if(specularEnabled)
        {
            RDGTextureHandle sssrResolve = builder.GetTexture("SSSR Resolve");

            // spec侧MV副本（copy pass落q0，紧跟G-Buffer后）：原velocity保持q1侧DiffLight段即释放
            RDGCopyPassHandle velocityCopyPass = builder.CreateCopyPass(GetName() + " Velocity Copy")
                .From(velocity)
                .To(velocitySpec)
                .Finish();

            // —— spec链全族q0（ForceGraphics）：Copy SSSR → GenViewZ(spec) → NRD-spec实例 ——
            RDGComputePassHandle pass2 = builder.CreateComputePass(GetName() + " Copy SSSR")
                .AddFlag(RDGPassFlags::ForceGraphicsQueue)
                .RootSignature(copySssrRootSignature)
                .ReadWrite(1, 1, 0, restirSpecularColor)
                .Read(1, 2, 0, sssrResolve)
                .Execute([&](RDGPassContext context) {

                    RHICommandListRef command = context.command;
                    command->SetComputePipeline(computePipeline[2]);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                    command->BindDescriptorSet(context.descriptors[1], 1);
                    command->PushConstants(&specSetting, sizeof(specSetting), SHADER_FREQUENCY_COMPUTE);
                    command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16),
                                        Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16),
                                        1);
                })
                .Finish();

            RDGComputePassHandle viewZSpecPass = builder.CreateComputePass(GetName() + " Generate View Z Spec")
                .AddFlag(RDGPassFlags::ForceGraphicsQueue)
                .RootSignature(viewZRootSignature)
                .Read(1, 0, 0, depth, VIEW_TYPE_2D, {TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1})
                .Read(1, 1, 0, diffuse)
                .Read(1, 2, 0, normal)
                .ReadWrite(1, 4, 0, restirSpecularColor)
                .ReadWrite(1, 5, 0, nrdViewZSpec)
                .ReadWrite(1, 6, 0, nrdNormalSpec)
                .Execute([&](RDGPassContext context) {

                    RHICommandListRef command = context.command;
                    command->SetComputePipeline(computePipeline[0]);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                    command->BindDescriptorSet(context.descriptors[1], 1);
                    command->PushConstants(&specSetting, sizeof(specSetting), SHADER_FREQUENCY_COMPUTE);
                    command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16),
                                        Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16),
                                        1);
                })
                .Finish();
        }

        if(diffuseEnabled)
        {
            // —— diffuse侧留q1（ReSTIR之后）：GenViewZ(diff) → NRD-diff实例 ——
            RDGComputePassHandle pass = builder.CreateComputePass(GetName() + " Generate View Z")
                .RootSignature(viewZRootSignature)
                .Read(1, 0, 0, depth, VIEW_TYPE_2D, {TEXTURE_ASPECT_DEPTH, 0, 1, 0, 1})
                .Read(1, 1, 0, diffuse)
                .Read(1, 2, 0, normal)
                .ReadWrite(1, 3, 0, restirDiffuseColor)
                .ReadWrite(1, 5, 0, nrdViewZ)
                .ReadWrite(1, 6, 0, nrdNormal)
                .Execute([&](RDGPassContext context) {

                    RHICommandListRef command = context.command;
                    command->SetComputePipeline(computePipeline[0]);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);
                    command->BindDescriptorSet(context.descriptors[1], 1);
                    command->PushConstants(&diffSetting, sizeof(diffSetting), SHADER_FREQUENCY_COMPUTE);
                    command->Dispatch(  Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().width, 16),
                                        Math::CeilDivide(EngineContext::Render()->GetWindowsExtent().height, 16),
                                        1);
                })
                .Finish();
        }

        {
            integrationSpec.NewFrame();
            integrationDiff.NewFrame();

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
            integrationSpec.SetCommonSettings(commonSettings);
            integrationDiff.SetCommonSettings(commonSettings);
    
            relaxSettings.enableAntiFirefly = setting.enableAntiFirefly > 0;
            relaxSettings.checkerboardMode = nrd::CheckerboardMode::OFF;
            integrationSpec.SetDenoiserSettings(0, &relaxSettings);        // RELAX_SPECULAR（实例内唯一denoiser，id=0）

            reblurSettings.hitDistanceParameters = {};
            reblurSettings.enableAntiFirefly = setting.enableAntiFirefly > 0;
            integrationDiff.SetDenoiserSettings(0, &reblurSettings);       // REBLUR_DIFFUSE

            if(specularEnabled)
            {
                // spec实例快照：viewZ/normal/MV全部用spec侧副本，与diff实例零共享纹理
                NRDResourceSnapshot specSnapshot = {};
                specSnapshot.SetResource(nrd::ResourceType::IN_NORMAL_ROUGHNESS, nrdNormalSpec);
                specSnapshot.SetResource(nrd::ResourceType::IN_MV, velocitySpec);
                specSnapshot.SetResource(nrd::ResourceType::IN_VIEWZ, nrdViewZSpec);
                specSnapshot.SetResource(nrd::ResourceType::IN_BASECOLOR_METALNESS, diffuse);
                specSnapshot.SetResource(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST, restirSpecularColor);
                specSnapshot.SetResource(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST, nrdOutSpecular);
                specSnapshot.SetResource(nrd::ResourceType::OUT_VALIDATION, nrdOutDebug);

                const nrd::Identifier specDenoisers[] = { 0 };
                integrationSpec.Denoise(builder, specDenoisers, 1, specSnapshot);
            }

            if(diffuseEnabled)
            {
                NRDResourceSnapshot diffSnapshot = {};
                diffSnapshot.SetResource(nrd::ResourceType::IN_NORMAL_ROUGHNESS, nrdNormal);
                diffSnapshot.SetResource(nrd::ResourceType::IN_MV, velocity);
                diffSnapshot.SetResource(nrd::ResourceType::IN_VIEWZ, nrdViewZ);
                diffSnapshot.SetResource(nrd::ResourceType::IN_BASECOLOR_METALNESS, diffuse);
                diffSnapshot.SetResource(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, restirDiffuseColor);
                diffSnapshot.SetResource(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST, nrdOutDiffuse);
                diffSnapshot.SetResource(nrd::ResourceType::OUT_VALIDATION, nrdOutDebug);

                const nrd::Identifier diffDenoisers[] = { 0 };
                integrationDiff.Denoise(builder, diffDenoisers, 1, diffSnapshot);
            }
        }

        if(!debug)
        {
            RDGComputePassHandle pass1 = builder.CreateComputePass(GetName() + " Combine")
                .RootSignature(combineRootSignature)
                .Read(1, 1, 0, diffuse)
                .Read(1, 2, 0, normal)
                .Read(1, 3, 0, nrdOutDiffuse)
                .Read(1, 4, 0, nrdOutSpecular)
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
