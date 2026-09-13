#pragma once

#include "RHIStructs.h"
#include "RHICommandList.h"
#include "Platform/HAL/Mutex.h"
#include "Platform/HAL/ScopeLock.h"
#include "Platform/HAL/PlatformProcess.h"

#include <cstdint>
#include <memory>
#include <queue>
#include <vector>

//RHI资源 ////////////////////////////////////////////////////////////////////////////////////////////////////////

class RHIResource
{
public:
	RHIResource() = delete;
	RHIResource(RHIResourceType resourceType) : resourceType(resourceType) {};
	virtual ~RHIResource() {};

	inline RHIResourceType GetType() { return resourceType; }

	virtual void* RawHandle() { return nullptr; };		// 底层资源的裸指针，仅debug时使用

private:
	RHIResourceType resourceType;
	uint32_t lastUseTick = 0;		// 最后一次使用的时间，帧单位

	virtual void Destroy() {};		// 资源销毁时调用，子类实现

	friend class RHIBackend;
};

//基本资源 ////////////////////////////////////////////////////////////////////////////////////////////////////////

class RHIQueue : public RHIResource
{
public:
	RHIQueue(const RHIQueueInfo& info)
	: RHIResource(RHI_QUEUE)
	, info(info)
	{}

	virtual void WaitIdle() = 0;

	// 队列族索引——RDG层判定跨队列资源是否需要所有权转移（release/acquire）用
	virtual uint32_t GetFamilyIndex() const = 0;

	inline QueueType GetQueueType() const { return info.type; }

protected:
	RHIQueueInfo info;
};

class RHISurface : public RHIResource
{
public:
	RHISurface() : RHIResource(RHI_SURFACE) {};

	inline Extent2D GetExetent() const { return extent; }

protected:
	Extent2D extent;
};

class RHISwapchain : public RHIResource
{
public:
	RHISwapchain(const RHISwapchainInfo& info) 
	: RHIResource(RHI_SWAPCHAIN)
	, info(info)
	{}

	virtual uint32_t GetCurrentFrameIndex() = 0;
	virtual RHITextureRef GetTexture(uint32_t index) = 0;
	virtual RHITextureRef GetNewFrame(RHIFenceRef fence, RHISemaphoreRef signalSemaphore) = 0;
	virtual void Present(RHISemaphoreRef waitSemaphore) = 0;

protected:
	RHISwapchainInfo info;
};

class RHICommandPool : public RHIResource, public std::enable_shared_from_this<RHICommandPool>
{
public:
	RHICommandPool(const RHICommandPoolInfo& info)
	: RHIResource(RHI_COMMAND_POOL)
	, info(info)
	, sync(PlatformProcess::CreateMutex())
	{}

	RHICommandListRef CreateCommandList(bool byPass = true);

	// 池绑定的队列（list 归属队列的显式化入口：构造提交批次/一致性断言用）
	inline RHIQueueRef GetQueue() const { return info.queue; }

protected:
	RHICommandPoolInfo info;

	std::queue<RHICommandContextRef> idleContexts = {};  // 空闲 Context 队列
	std::vector<RHICommandContextRef> contexts = {};     // 所有已分配的 Context

	// 线程安全锁
	MutexRef sync;

	// 将一个RHICommandList中包裹的Context返回到池中，由RHICommandList在析构时调用
	// （与CreateCommandList的取出对称加锁——多线程析构list时idleContexts是共享数据）
	void ReturnToPool(RHICommandContextRef commandContext)
	{
		ScopeLock lock(sync);
		idleContexts.push(commandContext);
	}
	friend class RHICommandList;
};

//缓冲，纹理，着色器，加速结构 ////////////////////////////////////////////////////////////////////////////////////////////////////////

class RHIBuffer : public RHIResource
{
public:
	RHIBuffer(const RHIBufferInfo& info) 
	: RHIResource(RHI_BUFFER)
	, info(info) 
	{}

	virtual void* Map() = 0;
	virtual void UnMap() = 0;

	inline const RHIBufferInfo& GetInfo() const {return info; }

protected:
	RHIBufferInfo info;
};

class RHITextureView : public RHIResource
{
public:
	RHITextureView(const RHITextureViewInfo& info) 
	: RHIResource(RHI_TEXTURE_VIEW)
	, info(info)
	{}

	inline const RHITextureViewInfo& GetInfo() const {return info; }

protected:
	RHITextureViewInfo info;
};

class RHITexture : public RHIResource
{
public:
	RHITexture(const RHITextureInfo& info) 
	: RHIResource(RHI_TEXTURE)
	, info(info)
	{}

	Extent3D MipExtent(uint32_t mipLevel);

	inline const TextureSubresourceRange& GetDefaultSubresourceRange() const 	{ return defaultRange; }
	inline const TextureSubresourceLayers& GetDefaultSubresourceLayers() const 	{ return defaultLayers; }

	inline const RHITextureInfo& GetInfo() const {return info; }

protected:
	RHITextureInfo info;

	TextureSubresourceRange defaultRange = {};
	TextureSubresourceLayers defaultLayers = {};
};

class RHISampler : public RHIResource
{
public:
	RHISampler(const RHISamplerInfo& info) 
	: RHIResource(RHI_SAMPLER)
	, info(info)
	{}

	inline const RHISamplerInfo& GetInfo() const { return info; }

protected:
	RHISamplerInfo info;
};

class RHIShader : public RHIResource
{
public:
	RHIShader(const RHIShaderInfo& info) 
	: RHIResource(RHI_SHADER)
	, info(info)
	{
		frequency = info.frequency;
	}

	ShaderFrequency GetFrequency() 				const { return frequency; }
	const ShaderReflectInfo& GetReflectInfo() 	const { return reflectInfo; }
	const RHIShaderInfo& GetInfo() 				const { return info; }

private:
	ShaderFrequency frequency;

protected:
	RHIShaderInfo info;
	ShaderReflectInfo reflectInfo;
};

class RHIShaderBindingTable : public RHIResource
{
public:
	RHIShaderBindingTable(const RHIShaderBindingTableInfo& info) 
	: RHIResource(RHI_SHADER_BINDING_TABLE)
	, info(info)
	{}

	const RHIShaderBindingTableInfo& GetInfo() const { return info; }

protected:
	RHIShaderBindingTableInfo info;
};

class RHITopLevelAccelerationStructure : public RHIResource
{
public:
	RHITopLevelAccelerationStructure(const RHITopLevelAccelerationStructureInfo& info)
	: RHIResource(RHI_TOP_LEVEL_ACCELERATION_STRUCTURE)
	, info(info)
	{}

	// 立即路径：准备+immediate录制+提交（仅构造期的一次性空构建等场景使用）
	virtual void Update(const std::vector<RHIAccelerationStructureInstanceInfo>& instanceInfos, bool build = false) = 0;

	// 纯CPU准备：instance数据写入持久映射buffer + 填充构建信息（不录任何命令）
	// 帧内路径由RDG的TLASUpdatePass调用：Build期Prepare，Execute期经命令流录制
	virtual void PrepareUpdate(const std::vector<RHIAccelerationStructureInstanceInfo>& instanceInfos, bool build = false) = 0;

	// AS存储buffer（RDG以imported buffer的形式为其建立依赖边与屏障）
	virtual RHIBufferRef GetStorageBuffer() const = 0;

	const RHITopLevelAccelerationStructureInfo& GetInfo() const { return info; }

protected:
	RHITopLevelAccelerationStructureInfo info;
};

class RHIBottomLevelAccelerationStructure : public RHIResource
{
public:
	RHIBottomLevelAccelerationStructure(const RHIBottomLevelAccelerationStructureInfo& info) 
	: RHIResource(RHI_BOTTOM_LEVEL_ACCELERATION_STRUCTURE)
	, info(info)
	{}

	const RHIBottomLevelAccelerationStructureInfo& GetInfo() const { return info; }

protected:
	RHIBottomLevelAccelerationStructureInfo info;
};

//根签名，描述符 ////////////////////////////////////////////////////////////////////////////////////////////////////////

class RHIRootSignature : public RHIResource	//对pipelinelayout, descriptorSetPool等的抽象
{
public:
	RHIRootSignature(const RHIRootSignatureInfo& info) 
	: RHIResource(RHI_ROOT_SIGNATURE)
	, info(info)
	{}

	virtual RHIDescriptorSetRef CreateDescriptorSet(uint32_t set) = 0;

	const RHIRootSignatureInfo& GetInfo() { return info; }

protected:
	RHIRootSignatureInfo info;
};

class RHIDescriptorSet : public RHIResource 
{
public:
	RHIDescriptorSet() : RHIResource(RHI_DESCRIPTOR_SET) {}

	virtual RHIDescriptorSet& UpdateDescriptor(const RHIDescriptorUpdateInfo& descriptorUpdateInfo) = 0;

	RHIDescriptorSet& UpdateDescriptors(const std::vector<RHIDescriptorUpdateInfo>& descriptorUpdateInfos) 
	{ 
		for(auto& info : descriptorUpdateInfos) UpdateDescriptor(info); 
		return *this;
	};
};

//管线状态 ////////////////////////////////////////////////////////////////////////////////////////////////////////

class RHIGraphicsPipeline : public RHIResource
{
public:
	RHIGraphicsPipeline(const RHIGraphicsPipelineInfo& info) 
	: RHIResource(RHI_GRAPHICS_PIPELINE) 
	, info(info)
	{}

	const RHIGraphicsPipelineInfo& GetInfo() { return info; }

protected:
	RHIGraphicsPipelineInfo info;
};

class RHIComputePipeline : public RHIResource
{
public:
	RHIComputePipeline(const RHIComputePipelineInfo& info) 
	: RHIResource(RHI_COMPUTE_PIPELINE)
	, info(info)
	{}

protected:
	RHIComputePipelineInfo info;
};

class RHIRayTracingPipeline : public RHIResource
{
public:
	RHIRayTracingPipeline(const RHIRayTracingPipelineInfo& info)
	: RHIResource(RHI_RAY_TRACING_PIPELINE) 
	, info(info)
	{}

protected:
	RHIRayTracingPipelineInfo info;
};

//同步 ////////////////////////////////////////////////////////////////////////////////////////////////////////

class RHIFence : public RHIResource
{
public:
	RHIFence()
	: RHIResource(RHI_FENCE) 
	{}

	virtual void Wait() = 0;
};

class RHISemaphore : public RHIResource
{
public:
	RHISemaphore()
	: RHIResource(RHI_SEMAPHORE)
	{}
};

// 渲染查询——GPU侧逐pass统计的采集与回读载体。RenderSystem在init期创建并跨帧持有，
// 每帧经RDGPerFrameResource传入RDG流水线（PassExecutionPhase录制期打点），
// 帧首fence等待后由RenderSystem调ResolveFrame回读输出。enable语义见RHIRenderQueryInfo
class RHIRenderQuery : public RHIResource
{
public:
	RHIRenderQuery(const RHIRenderQueryInfo& info)
	: RHIResource(RHI_RENDER_QUERY)
	, info(info)
	{}

	inline const RHIRenderQueryInfo& GetInfo() const { return info; }

	// 录制期打点：pass末尾GPU时间戳（slot=帧槽，queueIndex=队列下标，index=该队列流内pass下标——
	// worker并行录制时下标天然确定，无分配竞态；查询池按(槽,队列)二维分区，各流索引互不冲突）。
	// 默认空实现（仅Vulkan后端支持；须在BeginCommand后的录制中调用）
	virtual void WriteTimestamp(RHICommandListRef command, uint32_t slot, uint32_t queueIndex,
		uint32_t index, const std::string& passName) {}

	// 帧首回读（fence->Wait()后调用，此时上一帧GPU工作已全部完成）：按info中enable的项
	// 分派输出（enable_gpu_timing=逐pass耗时Top统计）。默认空实现
	virtual void ResolveFrame(uint32_t slot) {}

	// 按pass名的跨帧执行耗时估计（EMA，ResolveFrame持续更新；跨帧槽共享——同一渲染负载下
	// 两帧槽的pass耗时统计同源）。未命中返回fallback。供QueueSchedule的HEFT调度做权重
	// （2026-09-11：调度器从NodeType+白名单升级为EFT决策，需要pass级历史耗时）
	virtual double GetPassDurationMs(const std::string& passName, double fallbackMs) const { return fallbackMs; }

protected:
	RHIRenderQueryInfo info;
};

//TODO RenderQuery	StagingBuffer用于拷贝GPU到CPU




