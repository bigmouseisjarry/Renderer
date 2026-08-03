#pragma once

#include "Core/Math/Math.h"
#include "Function/Global/Definations.h"
#include "Function/Render/RenderResource/Buffer.h"
#include "Function/Render/RenderResource/RenderStructs.h"

#include "Function/Render/RenderResource/Shader.h"
#include "MeshPass.h"
#include "RenderPass.h"
#include <array>
#include <cstdint>
#include <vector>

class GPUCullingPass : public RenderPass
{
public:
	GPUCullingPass() = default;
	~GPUCullingPass() {};

	virtual void Init() override final;

	virtual void Build(RDGBuilder& builder) override final;

	virtual std::string GetName() override final { return "GPU Culling"; }

	virtual PassType GetType() override final { return GPU_CULLING_PASS; }

	void CollectStatisticDatas();

private:
	struct CullingLodSetting
	{
		uint32_t passType = 0;	// 当前处理的meshpass种类
		uint32_t passCount = 0;	// pass数量

		float lodErrorRate = 0.01f;
		float lodErrorOffset = 0.0f;
		uint32_t disableVirtualMeshCulling = 0;	// 强制虚拟几何体使用LOD0,在ReSTIR中需要光栅G-Buffer和加速结构的几何一致
		uint32_t disableOcclusionCulling = 0;
		uint32_t disableFrustrumCulling = 0;
		uint32_t showBoundingBox = 0;
		uint32_t enableStatistics = 0;
	};
	CullingLodSetting lodSetting = {};

    Shader cullingShader;
	Shader clusterGroupCullingShader;
	Shader clusterCullingShader;
    
    RHIRootSignatureRef rootSignature;
    RHIComputePipelineRef cullingPipeline;
	RHIComputePipelineRef clusterGroupCullingPipeline;
	RHIComputePipelineRef clusterCullingPipeline;

private:
	struct ReadBackData
	{
		IndirectSetting meshSetting;
		IndirectSetting clusterSetting;
		IndirectSetting clusterGroupSetting;
	};

	bool enableStatistics = false;
	std::array<std::vector<ReadBackData>, MESH_PASS_TYPE_MAX_CNT> readBackDatas;   

	EnablePassEditourUI()
};