#pragma once

#include "Function/Global/Definations.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/Buffer.h"
#include "RenderPass.h"
#include <cstdint>

class ReSTIRGIPass : public RenderPass
{
public:
    ReSTIRGIPass() {};
	~ReSTIRGIPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "ReSTIR GI"; }

	virtual PassType GetType() override final { return RESTIR_GI_PASS; }

private:
    Shader computeShader[3];

    RHIRootSignatureRef rootSignature;
    RHIDescriptorSetRef descriptorSet[2];
    RHIComputePipelineRef computePipeline[3];

    uint32_t currentIndex = 0;

	EnablePassEditourUI()

private:
    struct RestirGISetting
    {
        int initialSampleCount = 1;            // 每帧更新的数目

        int temporalSampleCountMultiplier = 20;     // 时域总采样数上限
        float temporalPosThreshold = 0.0001f;       // 时域重投影误差阈值

        float spatialPosThreshold = 2.0f;           // 空域误差阈值（仅有偏时使用）
        float spatialNormalThreshold = 45.0f;
        float spatialRadius = 30.0f;                // 空域采样像素半径

        float surfaceCacheFactor = 1.0f;            // 由于表面缓存由于无法处理镜面反射，且存在失效问题，实际得到的GI结果会偏暗，
                                                    // TODO 

        uint32_t combineMode = 0;                   // 立即合并/降噪后合并
        uint32_t visibilityReuse = 1;               // 开启可见性测试
        uint32_t temporalReuse = 1;                 // 开启时域重用
        uint32_t spatialReuse = 1;                  // 开启空域重用
        uint32_t unbias = 1;                        // 开启无偏
        uint32_t enableSkybox = 1;                  // 采样天空盒
        uint32_t showRadiance = 0;                  // 显示gather的入射radiance结果
    };
    RestirGISetting setting = {};

    struct HitSample 
    {
        Vec3 hitPos;
        float _padding0;
        Vec3 hitNormal;
        float _padding1;
        Vec3 outRadiance;
        float _padding2;
    };

    struct GIReservoir 
    {
        HitSample sampl;

        float pHat;					    // 当前光源的重要性采样权重，也就是targetPdf
        float sumWeights;			    // 已处理的(pHat / proposalPdf)权重和
        float w;					    // 被积函数在当前采样点对应的权重，同时也就是重采样重要性采样的realPdf(SIR PDF)的倒数
        uint32_t numStreamSamples;		// 已处理的采样总数
    };

    Buffer<RestirGISetting> restirSettingBuffer;
    ArrayBuffer<GIReservoir, WINDOW_WIDTH * WINDOW_HEIGHT> reservoirsBuffer[3] = {};
};