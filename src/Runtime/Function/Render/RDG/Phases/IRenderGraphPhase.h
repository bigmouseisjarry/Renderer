#pragma once

#include "Function/Render/RDG/RDGNode.h"
#include "Function/Render/RHI/RHICommandList.h"   // RHICommandPoolRef, RHICommandListRef 等

#include <vector>

class RDGBuilder;

// 每帧公共执行资源——作为 Phase 的执行上下文
struct RDGPerFrameResource
{
    RHICommandPoolRef   GraphicsPool;
    RHICommandListRef   GraphicsCommand;

    RHISemaphoreRef     startSemaphore;
    RHISemaphoreRef     finishSemaphore;

    RHIFenceRef         fence;

    // 本帧的RDGBuilder（构建图的来源），PassExecutionPhase 组装 RDGPassContext 时使用。
    // 由 RenderSystem 在 compile_and_execute 之前填入
    RDGBuilder*         builder = nullptr;

    // TODO 多队列提交扩展位：每队列的 pool/command/信号量，
    // 届时录制目标从 GraphicsCommand 单流改为按 optimized_timeline 分队列
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