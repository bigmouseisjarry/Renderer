#include "RDGNode.h"
#include "Core/DependencyGraph/DependencyGraph.h"
#include <utility>

void RDGTextureNode::ForEachPass(const std::function<void(RDGTextureEdgeRef, RDGPassNodeRef)>& func)
{
    for(auto& edge : InEdges<RDGTextureEdge>())     func(edge, edge->From<RDGPassNode>());
    for(auto& edge : OutEdges<RDGTextureEdge>())    func(edge, edge->To<RDGPassNode>());
}

void RDGBufferNode::ForEachPass(const std::function<void(RDGBufferEdgeRef, class RDGPassNode*)>& func)
{
    for(auto& edge : InEdges<RDGBufferEdge>())     func(edge, edge->From<RDGPassNode>());
    for(auto& edge : OutEdges<RDGBufferEdge>())    func(edge, edge->To<RDGPassNode>());
}

void RDGPassNode::ForEachTexture(const std::function<void(RDGTextureEdgeRef, RDGTextureNodeRef)>& func)
{
    auto inTextures = InEdges<RDGTextureEdge>();
    auto outTextures = OutEdges<RDGTextureEdge>();

    for(auto& textureEdge : inTextures)     func(textureEdge, textureEdge->From<RDGTextureNode>());
    for(auto& textureEdge : outTextures)    func(textureEdge, textureEdge->To<RDGTextureNode>());
}

void RDGPassNode::ForEachBuffer(const std::function<void(RDGBufferEdgeRef, RDGBufferNodeRef)>& func)
{
    auto inBuffers = InEdges<RDGBufferEdge>();
    auto outBuffers = OutEdges<RDGBufferEdge>();

    for(auto& bufferEdge : inBuffers)   func(bufferEdge, bufferEdge->From<RDGBufferNode>());
    for(auto& bufferEdge : outBuffers)  func(bufferEdge, bufferEdge->To<RDGBufferNode>());
}

void RDGDependencyGraph::Link(RDGPassNodeRef from, RDGTextureNodeRef to, RDGTextureEdgeRef edge)
{
    DependencyGraph::Link(from, to, edge);
    passTextures[from].emplace_back(edge, to);
    texturePasses[to].emplace_back(edge, from);
}

void RDGDependencyGraph::Link(RDGTextureNodeRef from, RDGPassNodeRef to, RDGTextureEdgeRef edge)
{
    DependencyGraph::Link(from, to, edge);
    passTextures[to].emplace_back(edge, from);
    texturePasses[from].emplace_back(edge, to);
}

void RDGDependencyGraph::Link(RDGPassNodeRef from, RDGBufferNodeRef to, RDGBufferEdgeRef edge)
{
    DependencyGraph::Link(from, to, edge);
    passBuffers[from].emplace_back(edge, to);
    bufferPasses[to].emplace_back(edge, from);
}

void RDGDependencyGraph::Link(RDGBufferNodeRef from, RDGPassNodeRef to, RDGBufferEdgeRef edge)
{
    DependencyGraph::Link(from, to, edge);
    passBuffers[to].emplace_back(edge, from);
    bufferPasses[from].emplace_back(edge, to);
}

void RDGDependencyGraph::ForEachPass(RDGTextureNodeRef texture, const std::function<void(RDGTextureEdgeRef, RDGPassNodeRef)>& func)
{
    for(auto& pair : texturePasses[texture])   
        func(pair.first, pair.second);
}

void RDGDependencyGraph::ForEachPass(RDGBufferNodeRef buffer, const std::function<void(RDGBufferEdgeRef, RDGPassNodeRef)>& func)
{
    for(auto& pair : bufferPasses[buffer])   
        func(pair.first, pair.second);
}

void RDGDependencyGraph::ForEachTexture(RDGPassNodeRef pass, const std::function<void(RDGTextureEdgeRef, RDGTextureNodeRef)>& func)
{
    for(auto& pair : passTextures[pass])   
        func(pair.first, pair.second);
}

void RDGDependencyGraph::ForEachBuffer(RDGPassNodeRef pass, const std::function<void(RDGBufferEdgeRef, RDGBufferNodeRef)>& func)
{
    for(auto& pair : passBuffers[pass])   
        func(pair.first, pair.second);
}