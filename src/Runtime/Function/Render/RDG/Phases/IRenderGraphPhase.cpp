#include "IRenderGraphPhase.h"

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