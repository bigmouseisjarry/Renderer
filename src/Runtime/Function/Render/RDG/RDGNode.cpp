#include "RDGNode.h"
#include "Core/DependencyGraph/DependencyGraph.h"
#include <utility>

RDGTextureNodeRef RDGDependencyGraph::CreateTextureNode(std::string name)
{
    RDGTextureNodeRef node = new RDGTextureNode(name);
    graph->insert(node);
    textureNodeMap[node->ID()] = node;
    return node;
}

RDGBufferNodeRef RDGDependencyGraph::CreateBufferNode(std::string name)
{
    RDGBufferNodeRef node = new RDGBufferNode(name);
    graph->insert(node);
    bufferNodeMap[node->ID()] = node;
    return node;
}

RDGRenderPassNodeRef RDGDependencyGraph::CreateRenderPassNode(std::string name)
{
    RDGRenderPassNodeRef node = new RDGRenderPassNode(name);
    graph->insert(node);
    passNodeMap[node->ID()] = node;
    return node;
}

RDGComputePassNodeRef RDGDependencyGraph::CreateComputePassNode(std::string name)
{
    RDGComputePassNodeRef node = new RDGComputePassNode(name);
    graph->insert(node);
    passNodeMap[node->ID()] = node;
    return node;
}

RDGRayTracingPassNodeRef RDGDependencyGraph::CreateRayTracingPassNode(std::string name)
{
    RDGRayTracingPassNodeRef node = new RDGRayTracingPassNode(name);
    graph->insert(node);
    passNodeMap[node->ID()] = node;
    return node;
}

RDGPresentPassNodeRef RDGDependencyGraph::CreatePresentPassNode(std::string name)
{
    RDGPresentPassNodeRef node = new RDGPresentPassNode(name);
    graph->insert(node);
    passNodeMap[node->ID()] = node;
    return node;
}

RDGCopyPassNodeRef RDGDependencyGraph::CreateCopyPassNode(std::string name)
{
    RDGCopyPassNodeRef node = new RDGCopyPassNode(name);
    graph->insert(node);
    passNodeMap[node->ID()] = node;
    return node;
}

RDGTextureNodeRef RDGDependencyGraph::GetTextureNode(DAGID id)
{
    auto it = textureNodeMap.find(id);
    return it != textureNodeMap.end() ? it->second : nullptr;
}

RDGBufferNodeRef RDGDependencyGraph::GetBufferNode(DAGID id)
{
    auto it = bufferNodeMap.find(id);
    return it != bufferNodeMap.end() ? it->second : nullptr;
}

RDGPassNodeRef RDGDependencyGraph::GetPassNode(DAGID id)
{
    auto it = passNodeMap.find(id);
    return it != passNodeMap.end() ? it->second : nullptr;
}

void RDGDependencyGraph::Link(RDGPassNodeRef from, RDGTextureNodeRef to, RDGTextureEdgeRef edge)
{
    graph->link(from, to, edge);
}

void RDGDependencyGraph::Link(RDGTextureNodeRef from, RDGPassNodeRef to, RDGTextureEdgeRef edge)
{
    graph->link(from, to, edge);
}

void RDGDependencyGraph::Link(RDGPassNodeRef from, RDGBufferNodeRef to, RDGBufferEdgeRef edge)
{
    graph->link(from, to, edge);
}

void RDGDependencyGraph::Link(RDGBufferNodeRef from, RDGPassNodeRef to, RDGBufferEdgeRef edge)
{
    graph->link(from, to, edge);
}

void RDGDependencyGraph::ForEachPass(RDGTextureNodeRef texture, const std::function<void(RDGTextureEdgeRef,RDGPassNodeRef)>& func)
{
    // 入边：pass → texture（pass 写入该 texture）
    graph->foreach_incoming_edges(texture,
        [&](DependencyGraphNode* from, DependencyGraphNode* /*to*/, DependencyGraphEdge* e) {
            func(static_cast<RDGTextureEdgeRef>(e), static_cast<RDGPassNodeRef>(from));
        });
    // 出边：texture → pass（pass 读取该 texture）
    graph->foreach_outgoing_edges(texture,
        [&](DependencyGraphNode* /*from*/, DependencyGraphNode* to, DependencyGraphEdge* e) {
            func(static_cast<RDGTextureEdgeRef>(e), static_cast<RDGPassNodeRef>(to));
        });
}

void RDGDependencyGraph::ForEachPass(RDGBufferNodeRef buffer, const std::function<void(RDGBufferEdgeRef,RDGPassNodeRef)>& func)
{
    graph->foreach_incoming_edges(buffer,
        [&](DependencyGraphNode* from, DependencyGraphNode* /*to*/, DependencyGraphEdge* e) {
            func(static_cast<RDGBufferEdgeRef>(e), static_cast<RDGPassNodeRef>(from));
        });
    graph->foreach_outgoing_edges(buffer,
        [&](DependencyGraphNode* /*from*/, DependencyGraphNode* to, DependencyGraphEdge* e) {
            func(static_cast<RDGBufferEdgeRef>(e), static_cast<RDGPassNodeRef>(to));
        });
}

// pass 节点既有 texture 边也有 buffer 边，需要按 EdgeType() 过滤
void RDGDependencyGraph::ForEachTexture(RDGPassNodeRef pass, const std::function<void(RDGTextureEdgeRef,RDGTextureNodeRef)>& func)
{
    // 入边：texture → pass（pass 读取 texture）
    graph->foreach_incoming_edges(pass,
        [&](DependencyGraphNode* from, DependencyGraphNode* /*to*/, DependencyGraphEdge* e) {
            if (static_cast<RDGEdge*>(e)->EdgeType() == RDG_EDGE_TYPE_TEXTURE) {
                func(static_cast<RDGTextureEdgeRef>(e), static_cast<RDGTextureNodeRef>(from));
            }
        });
    // 出边：pass → texture（pass 写入 texture）
    graph->foreach_outgoing_edges(pass,
        [&](DependencyGraphNode* /*from*/, DependencyGraphNode* to, DependencyGraphEdge* e) {
            if (static_cast<RDGEdge*>(e)->EdgeType() == RDG_EDGE_TYPE_TEXTURE) {
                func(static_cast<RDGTextureEdgeRef>(e), static_cast<RDGTextureNodeRef>(to));
            }
        });
}

void RDGDependencyGraph::ForEachBuffer(RDGPassNodeRef pass, const std::function<void(RDGBufferEdgeRef,RDGBufferNodeRef)>& func)
{
    graph->foreach_incoming_edges(pass,
        [&](DependencyGraphNode* from, DependencyGraphNode* /*to*/, DependencyGraphEdge* e) {
            if (static_cast<RDGEdge*>(e)->EdgeType() == RDG_EDGE_TYPE_BUFFER) {
                func(static_cast<RDGBufferEdgeRef>(e), static_cast<RDGBufferNodeRef>(from));
            }
        });
    graph->foreach_outgoing_edges(pass,
        [&](DependencyGraphNode* /*from*/, DependencyGraphNode* to, DependencyGraphEdge* e) {
            if (static_cast<RDGEdge*>(e)->EdgeType() == RDG_EDGE_TYPE_BUFFER) {
                func(static_cast<RDGBufferEdgeRef>(e), static_cast<RDGBufferNodeRef>(to));
            }
        });
}

void RDGDependencyGraph::ForEachTextureNode(const std::function<void(RDGTextureNodeRef)>& func)
{
    for (auto& [id, node] : textureNodeMap) func(node);
}

void RDGDependencyGraph::ForEachBufferNode(const std::function<void(RDGBufferNodeRef)>& func)
{
    for (auto& [id, node] : bufferNodeMap) func(node);
}

void RDGDependencyGraph::ForEachPassNode(const std::function<void(RDGPassNodeRef)>& func)
{
    for (auto& [id, node] : passNodeMap) func(node);
}

void RDGDependencyGraph::ForEachEdge(const std::function<void(DependencyGraphNode* from, DependencyGraphNode* to,
    DependencyGraphEdge* edge)>& func)
{
    graph->foreach_edges(func);
}