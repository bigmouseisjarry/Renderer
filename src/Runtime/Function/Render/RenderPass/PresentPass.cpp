#include "PresentPass.h"
#include "Function/Global/EngineContext.h"

void PresentPass::Init()
{

}   

void PresentPass::Build(RDGBuilder& builder) 
{
    RHITextureRef swapchainTexture = EngineContext::Render()->GetSwapchain()->GetTexture(EngineContext::ThreadPool()->ThreadFrameIndex());
    
    RDGTextureHandle outColor = builder.GetTexture("Final LDR Color");

    RDGTextureHandle present = builder.CreateTexture("Present")
        .Import(swapchainTexture)   // 帧首UNDEFINED兜底/记录稳态PREVENT与手工屏障环自洽（pass内PRESENT↔TRANSFER_DST手工循环）
        .Finish();

    RDGPresentPassHandle pass = builder.CreatePresentPass(GetName())
        .PresentTexture(present)
        .Texture(outColor)
        .Finish();
}
