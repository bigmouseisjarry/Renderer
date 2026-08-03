#pragma once

#include "Core/Math/Math.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "RenderPass.h"
#include <cstdint>

class IBLPass : public RenderPass
{
public:
    IBLPass() {};
	~IBLPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "IBL"; }

	virtual PassType GetType() override final { return IBL_PASS; }

private:
    struct IBLSetting {
        Vec4 front;
        Vec4 up;
        
        float deltaPhi = (2.0f * float(PI)) / 180.0f;
        float deltaTheta = (0.5f * float(PI)) / 64.0f;

        float roughness;
        uint32_t numSamples = 32;
        uint32_t mip;
    };
    // IBLSetting setting = {};

    Shader computeShader[2];

    RHIRootSignatureRef rootSignature;
    RHIComputePipelineRef computePipeline[2];

    TextureRef brdfTex;


    //法线方向，参考点光源的ogl矩阵向量
    std::vector<Vec4> fronts =
    {
        Vec4(1.0,   0.0,	    0.0,	    0.0),
        Vec4(-1.0,  0.0,	    0.0,	    0.0),
        Vec4(0.0,   1.0,	    0.0,	    0.0),
        Vec4(0.0,   -1.0,	    0.0,	    0.0),
        Vec4(0.0,   0.0,	    1.0,	    0.0),
        Vec4(0.0,   0.0,	    -1.0,	0.0),
    };

    std::vector<Vec4> ups =
    {
        Vec4(0.0,   -1.0,	    0.0,	    0.0),
        Vec4(0.0,   -1.0,	    0.0,	    0.0),
        Vec4(0.0,   0.0,	    1.0,	    0.0),
        Vec4(0.0,   0.0,	    -1.0,	0.0),
        Vec4(0.0,   -1.0,	    0.0,	    0.0),
        Vec4(0.0,   -1.0,	    0.0,	    0.0),
    };

    int mip;

	EnablePassEditourUI()
};