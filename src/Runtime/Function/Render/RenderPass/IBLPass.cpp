#include "IBLPass.h"
#include "Function/Global/Definations.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <cstdint>

void IBLPass::Init()
{
    auto backend = EngineContext::RHI();

    computeShader[0] = Shader(EngineContext::File()->ShaderPath() + "ibl/ibl_diffuse.comp.spv", SHADER_FREQUENCY_COMPUTE);
    computeShader[1] = Shader(EngineContext::File()->ShaderPath() + "ibl/ibl_specular.comp.spv", SHADER_FREQUENCY_COMPUTE);

    RHIRootSignatureInfo rootSignatureInfo = {};
    rootSignatureInfo.AddEntry(EngineContext::RenderResource()->GetPerFrameRootSignature()->GetInfo())
                     .AddEntry({1, 0, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE})
                     .AddPushConstant({128, SHADER_FREQUENCY_COMPUTE});
    rootSignature = backend->CreateRootSignature(rootSignatureInfo);

    RHIComputePipelineInfo pipelineInfo     = {};
    pipelineInfo.rootSignature              = rootSignature;

    pipelineInfo.computeShader              = computeShader[0].shader;
    computePipeline[0]   = backend->CreateComputePipeline(pipelineInfo);

    pipelineInfo.computeShader              = computeShader[1].shader;
    computePipeline[1]   = backend->CreateComputePipeline(pipelineInfo);


    brdfTex = std::make_shared<Texture>("Asset/BuildIn/Texture/ibl/ibl_brdf_lut.png");
}   

void IBLPass::Build(RDGBuilder& builder) 
{
    RDGTextureHandle brdfLut = builder.CreateTexture("BRDF LUT")
        .Import(brdfTex->texture, RESOURCE_STATE_UNDEFINED)
        .Finish(); 

    if(IsEnabled())
    {
        SetEnable(false);   // 只执行一次

        RDGTextureHandle diffuseTex = builder.CreateTexture("Skybox IBL diffuse")
            .Import(EngineContext::RenderResource()->GetIBLTexture(0), RESOURCE_STATE_UNDEFINED)
            .Finish();

        RDGTextureHandle specularTex = builder.CreateTexture("Skybox IBL specular")
            .Import(EngineContext::RenderResource()->GetIBLTexture(1), RESOURCE_STATE_UNDEFINED)
            .Finish();

        for(uint32_t i = 0; i < 6; i++)
        {
            std::string index = " [" + std::to_string(i) + "]";

            auto passBuilder = builder.CreateComputePass(GetName() + " diffuse" + index)
                .PassIndex(i)
                .RootSignature(rootSignature)
                .ReadWrite(1, 0, 0, diffuseTex, VIEW_TYPE_2D, { TEXTURE_ASPECT_COLOR, 0, 1, i, 1 })
                .Execute([&](RDGPassContext context) {       

                    IBLSetting setting = {};
                    setting.front = fronts[context.passIndex[0]];
                    setting.up = ups[context.passIndex[0]];

                    RHICommandListRef command = context.command; 
                    command->SetComputePipeline(computePipeline[0]);
                    command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);   
                    command->BindDescriptorSet(context.descriptors[1], 1);
                    command->PushConstants(&setting, sizeof(IBLSetting), SHADER_FREQUENCY_COMPUTE);
                    command->Dispatch(  DIFFUSE_IBL_SIZE / 16, 
                                        DIFFUSE_IBL_SIZE / 16, 
                                        1);
                });
                
            if(i == 5) // 最后一级手动屏障 TODO
                passBuilder.OutputRead(diffuseTex);
            passBuilder.Finish();
        }   

        mip = (int)(floor(log2(SPECULAR_IBL_SIZE))) + 1;
        for(uint32_t i = 0; i < 6; i++)
        {
            for (uint32_t j = 0; j < mip; j++)
            {
                std::string index = " [" + std::to_string(i) + "]" + "[" + std::to_string(j) + "]";

                auto passBuilder = builder.CreateComputePass(GetName() + " specular" + index)
                    .PassIndex(i, j)
                    .RootSignature(rootSignature)
                    .ReadWrite(1, 0, 0, specularTex, VIEW_TYPE_2D, { TEXTURE_ASPECT_COLOR, j, 1, i, 1 })
                    .Execute([&](RDGPassContext context) {       

                        Extent2D extent = { SPECULAR_IBL_SIZE , SPECULAR_IBL_SIZE };
                        int mipLevel    = context.passIndex[1];
                        extent.width    = std::max((int)std::ceil(extent.width / (std::pow(2, mipLevel) * 16)), 1);
                        extent.height   = std::max((int)std::ceil(extent.height / (std::pow(2, mipLevel) * 16)), 1);

                        IBLSetting setting = {};
                        setting.front = fronts[context.passIndex[0]];
                        setting.up = ups[context.passIndex[0]];
                        setting.roughness = (float)context.passIndex[1] / (float)(mip - 1);	//[0,1],mip越高越粗糙
                        setting.mip = context.passIndex[1];

                        RHICommandListRef command = context.command; 
                        command->SetComputePipeline(computePipeline[1]);
                        command->BindDescriptorSet(EngineContext::RenderResource()->GetPerFrameDescriptorSet(), 0);   
                        command->BindDescriptorSet(context.descriptors[1], 1);
                        command->PushConstants(&setting, sizeof(IBLSetting), SHADER_FREQUENCY_COMPUTE);
                        command->Dispatch(  extent.width, 
                                            extent.height, 
                                            1);
                    });
                 
                if(i == 5 && j == mip - 1) // 最后一级手动屏障 TODO
                    passBuilder.OutputRead(specularTex);
                passBuilder.Finish();
            }
        }   
    }
}
