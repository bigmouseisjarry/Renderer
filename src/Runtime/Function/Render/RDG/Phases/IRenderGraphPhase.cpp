#include "IRenderGraphPhase.h"

#include "Function/Global/EngineContext.h"

void RDGQueueFrameSlot::EnsureCommandCount(uint32_t count)
{
    // 主线程规划期调用（录制dispatch前），无并发；每条新流独占一个新池
    while (commands.size() < count)
    {
        RHICommandPoolRef pool = EngineContext::RHI()->CreateCommandPool({ queue });
        pools.push_back(pool);
        commands.push_back(pool->CreateCommandList(true));   // byPass=true：立即录制
    }
}

IRenderGraphPhase::~IRenderGraphPhase()
{

}

void IRenderGraphPhase::reset_for_frame()
{

}

void IRenderGraphPhase::on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{

}

std::vector<RDGResourceNodeRef>& IRenderGraphPhase::get_resources(RDGDependencyGraphRef graph)
{
    return graph->resources;
}

std::vector<RDGPassNodeRef>& IRenderGraphPhase::get_passes(RDGDependencyGraphRef graph)
{
    return graph->passes;
}