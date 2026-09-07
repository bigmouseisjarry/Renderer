#pragma once

#include "Function/Render/RDG/RDGNode.h"
#include "Function/Render/RHI/RHICommandList.h"   // RHICommandPoolRef, RHICommandListRef 等

#include <vector>

// 每队列的帧槽录制资源——与 QueueSchedule::all_queues 下标对齐
// （RenderSystem 按 QueueSchedule::QueryConfiguredQueues 预建，Phase 8 断言对齐）
struct RDGQueueFrameSlot
{
    RHIQueueRef queue = nullptr;

    // 本队列专属时间线信号量：跨队列同步点的signal端 + 队列末批queueDone。
    // 每队列独立一条的原因：同一vkQueueSubmit内wait值必须小于signal值——单条共享时间线在
    // 跨队列乒乓依赖（A等B、B等A）下无解；按队列分线后，队列X的批次只signal自己的时间线、
    // 只wait他队时间线，约束恒满足
    RHISemaphoreRef timeline = nullptr;
    uint64_t timelineValueBase = 0;    // 运行计数；Phase 8每帧推进（帧槽fence保证旧值已终定）

    // 每命令流独立pool + byPass列表（独占context）：vkBegin/vkEnd/vkResetCommandBuffer要求父
    // VkCommandPool外部同步，并行录制时各worker同时BeginCommand必须物理隔离；
    // 跨帧复用由帧槽fence保证（帧首Wait后BeginCommand重置）
    std::vector<RHICommandPoolRef> pools;
    std::vector<RHICommandListRef> commands;

    // 主线程规划期按需增长命令流数（同步点密集时批次数可能超过预建数）。
    // 每条新流配一个新池（外部同步隔离）。非线程安全——仅规划期（录制dispatch前）调用
    void EnsureCommandCount(uint32_t count);
};

// 每帧公共执行资源——作为 Phase 的执行上下文
// 多队列执行：Phase 8 产出整帧提交计划（RHIQueueSubmitPlan）写入 submitPlan，
// RenderSystem::SubmitRHI（RHI线程）消费计划做逐批次多队列vkQueueSubmit2
struct RDGPerFrameResource
{

    RHISemaphoreRef     startSemaphore;     // 二进制：swapchain acquire（graphics首批次wait）
    RHISemaphoreRef     finishSemaphore;    // 二进制：present（Present所在批次signal）

    RHIFenceRef         fence;              // join批signal（CPU帧同步，Tick帧首Wait，语义不变）

    // 渲染查询（GPU侧逐pass统计）：RenderSystem每帧灌入（info中无任何enable时传nullptr），
    // PassExecutionPhase录制期消费（CommandRecordingConfig::enable_gpu_timing共同判定打点）
    RHIRenderQueryRef   renderQuery = nullptr;

    std::vector<RDGQueueFrameSlot> queueSlots;

    // Phase 8主线程填充（全部录制规划完成后），SubmitRHI按数组序逐批提交
    RHIQueueSubmitPlan  submitPlan;
};

struct IRenderGraphPhase
{
    using PerFrameCommonResourceRef = RDGPerFrameResource*;

    virtual ~IRenderGraphPhase();
    virtual void reset_for_frame();
    virtual void on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor);

    static std::vector<RDGResourceNodeRef>& get_resources(RDGDependencyGraphRef graph);
    static std::vector<RDGPassNodeRef>& get_passes(RDGDependencyGraphRef graph);
};
