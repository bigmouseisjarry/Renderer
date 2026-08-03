#pragma once

#include "Function/Render/RHI/RHIStructs.h"
#include "RenderPass.h"

// 依托史 太伟大了NV
class SVGFPass : public RenderPass
{
public:
    SVGFPass() {};
	~SVGFPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "SVGF"; }

	virtual PassType GetType() override final { return SVGF_PASS; }

private:
    struct SVGFSetting 
    {
        int round = 0;     

        // 测试下来静帧可用的设置： // 动态需要maxRound = 7~8, sigmaL = 4.0f, 仍不能完全避免噪点
        int maxRound = 2;  
        float sigmaP = 1.0f;  
        float sigmaN = 128.0f;   
        float sigmaL = 0.2f;    // 阴影只能靠亮度项双边滤波，开大了就会丢失阴影细节，开小了会有噪点？

        // 论文给出的参数设置：
        // int maxRound = 5;
        // float sigmaP = 1.0f;  
        // float sigmaN = 128.0f;   
        // float sigmaL = 4.0f;

        float alpha = 0.05f;  
        int mode = 0;

        int denoisedOnly = 0;       // 只显示降噪输入的部分
        int disocclusionFix = 1;    
        int antiFirefly = 1;   
        int historyClamp = 0;           // TODO 还有很大的问题，开启会导致GI降噪质量变得很差
    };                                  // 不开又会让镜面反射拖尾，光照也会有很大延迟
    SVGFSetting setting = {};
    bool denoisedOnly = false;

    Shader computeShader[7];

    RHIRootSignatureRef rootSignature;
    RHIComputePipelineRef computePipeline[7];
    RHITextureRef historyMomentsTex;
    RHITextureRef historyColorTex;
    RHITextureRef currentColorTex[2];

	EnablePassEditourUI()
};