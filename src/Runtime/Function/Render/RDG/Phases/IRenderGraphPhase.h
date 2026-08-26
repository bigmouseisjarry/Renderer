#pragma once

#include "Function/Render/RDG/RDGNode.h"
#include "Function/Render/RHI/RHICommandList.h"   // RHICommandPoolRef, RHICommandListRef 等

#include <vector>

class RDGBuilder;

// 每帧公共执行资源——作为 Phase 的执行上下文
struct RDGPerFrameResource
{

    RHISemaphoreRef     startSemaphore;
    RHISemaphoreRef     finishSemaphore;

    RHIFenceRef         fence;

    // 本帧的RDGBuilder（构建图的来源），PassExecutionPhase 组装 RDGPassContext 时使用。
    // 由 RenderSystem 在 compile_and_execute 之前填入
    RDGBuilder*         builder = nullptr;

    // chunked并行录制：按帧槽持久持有的命令流（byPass=true，各自独占一个context），
    // 由 RenderSystem::EnsureFrameChunkLists 惰性创建；每帧由各chunk的 BeginCommand 重置，
    // 帧槽 fence 保证跨帧复用安全。录制完成后由 SubmitRHI 用 ExecuteBatch 按序单次提交
    std::vector<RHICommandListRef> ChunkCommands;
    uint32_t            chunkCount = 0;     // 本帧启用的chunk数（提交 [0, chunkCount) ）

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