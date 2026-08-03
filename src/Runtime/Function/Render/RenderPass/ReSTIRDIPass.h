
// ReSTIR 
// https://dl.acm.org/doi/10.1145/3386569.3392481
// https://github.com/lukedan/ReSTIR-Vulkan
// https://zhuanlan.zhihu.com/p/703950102

// 1.蒙特卡洛积分：连续积分转为带权的离散采样求和，权值为1 / 采样点概率，无偏（求和期望就是积分结果）
// -需要选择合适的样本概率分布，快速拟合        --> 重要性采样
// -确定概率分布后，需要生成对应分布的样本      --> 采样分布映射

// 2.重要性采样：最佳分布就是原被积函数的pdf，即权值为1 / pdf(x_i)

// 3.采样分布映射：已知一个概率分布求对应的样本集合
// -样本重要性重采样：从一个更简单的p(x)分布中构建样本，以权重w(x_i) = p_hat(x_i) / p(x_i)归一化后作为选择概率
// -p(x):       proposal pdf
// -p_hat(x):   target pdf
// -采样数趋近无穷时，分布收敛到target pdf，实际是SIR PDf
// -样本重要性重采样并不要求pdf(x_i)归一化，也就是说可以直接使用目标被积函数作为 target pdf
// -ReSTIR中，目标函数使用的是去掉可见项的被积函数

// 4.加权蓄水池采样：流式的对一组带权样本选择其中一个
// -存储已处理的权重和，对于新的待处理样本x_i，生成[0,1]均匀随机数r, w_sum += w(x_i); r <= w(x_i) / w_sum 则更新样本
// -多个reservoir可以以相同的方式合并

// 5.多重重要性采样：来自多个不同分布的重要性采样的合并
// 对光源采样的proposal pdf都是相同的
// 时域相邻帧的Reservoir（不考虑移动时）的target pdf是相同的
// 空域的不同Reservoir的target pdf（被积函数）是不同的

#pragma once

#include "Function/Global/Definations.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/Buffer.h"
#include "RenderPass.h"
#include <cstdint>

#define NUM_NEIGHBORS 4

class ReSTIRDIPass : public RenderPass
{
public:
    ReSTIRDIPass() {};
	~ReSTIRDIPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "ReSTIR DI"; }

	virtual PassType GetType() override final { return RESTIR_DI_PASS; }

private:
    Shader computeShader[3];

    RHIRootSignatureRef rootSignature;
    RHIDescriptorSetRef descriptorSet[2];
    RHIComputePipelineRef computePipeline[3];

    uint32_t currentIndex = 0;

	EnablePassEditourUI()

private:
    struct RestirDISetting
    {
        int initialSampleCount = 2;            // 每帧更新的数目

        int temporalSampleCountMultiplier = 20;     // 时域总采样数上限
        float temporalPosThreshold = 0.0001f;       // 时域重投影误差阈值

        float spatialPosThreshold = 2.0f;           // 空域误差阈值（仅有偏时使用）
        float spatialNormalThreshold = 45.0f;
        float spatialRadius = 30.0f;                // 空域采样像素半径

        uint32_t combineMode = 0;                   // 立即合并/降噪后合并
        uint32_t visibilityReuse = 1;               // 开启可见性测试
        uint32_t temporalReuse = 1;                 // 开启时域重用
        uint32_t spatialReuse = 1;                  // 开启空域重用
        uint32_t unbias = 1;                        // 开启无偏
        uint32_t bruteLighting = 0;                 // 暴力遍历光源的基线
        uint32_t showWeight = 0;                    // 可视化权重
        uint32_t showLightID = 0;                   // 可视化选择的光源ID
    };
    RestirDISetting setting = {};

    struct LightSample 
    {
        uint32_t lightID; 			// 光源索引
    };

    struct DIReservoir 
    {
        LightSample sampl;

        float pHat;					    // 当前光源的重要性采样权重，也就是targetPdf
        float sumWeights;			    // 已处理的(pHat / proposalPdf)权重和
        float w;					    // 被积函数在当前采样点对应的权重，同时也就是重采样重要性采样的realPdf(SIR PDF)的倒数
        uint32_t numStreamSamples;		// 已处理的采样总数
    };

    Buffer<RestirDISetting> restirSettingBuffer;
    ArrayBuffer<DIReservoir, WINDOW_WIDTH * WINDOW_HEIGHT> reservoirsBuffer[3] = {};
};