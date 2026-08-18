#pragma once

#include "Function/Render/RDG/RDGNode.h"
#include "Function/Render/RHI/RHICommandList.h"   // RHICommandPoolRef, RHICommandListRef 等

#include <vector>

// 每帧公共执行资源——作为 Phase 的执行上下文
struct RDGPerFrameResource
{
    RHICommandPoolRef   GraphicsPool;
    RHICommandListRef   GraphicsCommand;

    RHISemaphoreRef     startSemaphore;
    RHISemaphoreRef     finishSemaphore;

    RHIFenceRef         fence;
};

struct IRenderGraphPhase
{
    using PerFrameCommonResourceRef = RDGPerFrameResource*;

    virtual ~IRenderGraphPhase();
    virtual void on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor);

    static std::vector<RDGResourceNodeRef>& get_resources(RDGDependencyGraphRef graph);
    static std::vector<RDGPassNodeRef>& get_passes(RDGDependencyGraphRef graph);
};