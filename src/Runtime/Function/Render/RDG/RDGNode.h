#pragma once

#include "Function/Render/RDG/RDGEdge.h"
#include "Function/Render/RHI/RHICommandList.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Core/DependencyGraph/DependencyGraph.h"
#include "Function/Render/RHI/RHIStructs.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class RDGBuilder;
class RDGDependencyGraph;

enum RDGPassNodeType
{
    RDG_PASS_NODE_TYPE_RENDER = 0,
    RDG_PASS_NODE_TYPE_COMPUTE,
    RDG_PASS_NODE_TYPE_RAY_TRACING,
    RDG_PASS_NODE_TYPE_PRESENT,
    RDG_PASS_NODE_TYPE_COPY,

    RDG_PASS_NODE_TYPE_MAX_ENUM,    //
};

enum RDGResourceNodeType
{
    RDG_RESOURCE_NODE_TYPE_TEXTURE = 0,
    RDG_RESOURCE_NODE_TYPE_BUFFER,

    RDG_RESOURCE_NODE_TYPE_MAX_ENUM,    //
};

struct RDGPassContext
{
    RHICommandListRef command;
    RDGBuilder* builder;
    std::array<RHIDescriptorSetRef, MAX_DESCRIPTOR_SETS> descriptors;

    uint32_t passIndex[3] = { 0, 0, 0};

};

using RDGPassExecuteFunc = std::function<void(RDGPassContext)> ;

class RDGNode : public DependencyGraphNode
{
public:
    RDGNode(std::string name) 
    : name(name)
    {}

    const std::string& Name() { return name; }

private:
    std::string name;
};
using RDGNodeRef = RDGNode* ;

// 资源节点/////////////////////////////////////////////////////////////////////////////////////

class RDGResourceNode : public RDGNode
{
public:
    RDGResourceNode(std::string name, RDGResourceNodeType nodeType) 
    : RDGNode(name) 
    , nodeType(nodeType)
    {}

    inline bool IsImported() { return isImported; }

    RDGResourceNodeType NodeType() { return nodeType; }

protected:
    RDGResourceNodeType nodeType;
    bool isImported = false;
};
using RDGResourceNodeRef = RDGResourceNode*;

// 纹理节点
class RDGTextureNode : public RDGResourceNode
{
public:
    RDGTextureNode(std::string name) 
    : RDGResourceNode(name, RDG_RESOURCE_NODE_TYPE_TEXTURE) 
    {}

    RDGTextureHandle GetHandle() { return RDGTextureHandle(ID()); } 

    const RHITextureInfo& GetInfo() { return info; }

    uint64_t get_size() const {
        // 从 RHITextureInfo 算出来
        return info.get_size();
    }

private:  
    RHITextureInfo info;
    RHIResourceState initState; // 从池中/外部引用时的最初状态

    RHITextureRef texture;      // 执行时分配和绑定，会动态更新，在最后一个依赖pass完成后返回资源池

    friend class RDGTextureBuilder;
    friend class RDGBuilder;
};
using RDGTextureNodeRef = RDGTextureNode*;

// 缓冲节点
class RDGBufferNode : public RDGResourceNode
{
public:
    RDGBufferNode(std::string name) 
    : RDGResourceNode(name, RDG_RESOURCE_NODE_TYPE_BUFFER) 
    {}

    RDGBufferHandle GetHandle() { return RDGBufferHandle(ID()); }

    uint64_t get_size() const { return info.size; }
    const RHIBufferInfo& GetInfo() { return info; }

private:  
    RHIBufferInfo info;
    RHIResourceState initState; // 从池中/外部引用时的最初状态

    RHIBufferRef buffer;        // 执行时分配和绑定，会动态更新，在最后一个依赖pass完成后返回资源池

    friend class RDGBufferBuilder;
    friend class RDGBuilder;
};
using RDGBufferNodeRef = RDGBufferNode*;

// pass节点/////////////////////////////////////////////////////////////////////////////////////

struct SamplerBind
{
    RHISamplerRef sampler;
    uint32_t set;
    uint32_t binding;
    uint32_t index;
};

enum class RDGPassFlags : uint32_t
{
    None = 0x0,
    SeparateFromCommandBuffer = 0x1,
    PreferAsyncCompute = 0x2,
    ForceGraphicsQueue = 0x4,
    ComputeIntensive = 0x10,
    VertexBoundIntensive = 0x20,
    PixelBoundIntensive = 0x40,
    BandwidthIntensive = 0x80,
    SmallWorkingSet = 0x100,
    LargeWorkingSet = 0x200,
    RandomAccess = 0x400,
    StreamingAccess = 0x800,
};

class RDGPassNode : public RDGNode
{
public:
    RDGPassNode(std::string name, RDGPassNodeType nodeType) 
    : RDGNode(name) 
    , nodeType(nodeType)
    {}

    inline bool Before(RDGPassNode* other)  { return ID() < other->ID(); }    // 假定所有pass的添加顺序就是执行顺序
    inline bool After(RDGPassNode* other)   { return ID() > other->ID(); }
    RDGPassNodeType NodeType() { return nodeType; }

    // 资源边遍历
    uint32_t textures_count() const {return static_cast<uint32_t>(textureEdges.size());}
    uint32_t buffers_count() const {return static_cast<uint32_t>(bufferEdges.size());}

    void foreach_textures(const std::function<void(RDGTextureNodeRef, RDGTextureEdgeRef)>& func) {
        for (auto& [tex, edge] : textureEdges) func(tex, edge);
    }
    void foreach_buffers(const std::function<void(RDGBufferNodeRef, RDGBufferEdgeRef)>& func) {
        for (auto& [buf, edge] : bufferEdges) func(buf, edge);
    }

    // 性能提示
    void set_flags(RDGPassFlags flags) { hintFlags = flags; }
    void add_flags(RDGPassFlags flags) {
        hintFlags = static_cast<RDGPassFlags>(static_cast<uint32_t>(hintFlags) | static_cast<uint32_t>(flags));
    }
    bool has_flags(RDGPassFlags flags) const {
        return (static_cast<uint32_t>(hintFlags) & static_cast<uint32_t>(flags)) != 0;
    }
    RDGPassFlags get_flags() const { return hintFlags; }

    friend class RDGDependencyGraph;

protected:
    RDGPassNodeType nodeType;
    bool isCulled = false;

    RHIRootSignatureRef rootSignature;
    std::array<RHIDescriptorSetRef, MAX_DESCRIPTOR_SETS> descriptorSets;

    std::vector<RHITextureViewRef> pooledViews;             // 动态分配的池化资源，执行完毕后返回资源池
    std::vector<std::pair<RHIDescriptorSetRef, uint32_t>> pooledDescriptorSets;
    std::vector<SamplerBind> samplers;                

    friend class RDGBuilder;

    RDGPassFlags hintFlags = RDGPassFlags::None;
    std::vector<std::pair<RDGTextureNodeRef, RDGTextureEdgeRef>> textureEdges;
    std::vector<std::pair<RDGBufferNodeRef, RDGBufferEdgeRef>> bufferEdges;

};
using RDGPassNodeRef = RDGPassNode* ;


class RDGRenderPassNode : public RDGPassNode
{
public:
    RDGRenderPassNode(std::string name) 
    : RDGPassNode(name, RDG_PASS_NODE_TYPE_RENDER) 
    {}

    RDGRenderPassHandle GetHandle() { return RDGRenderPassHandle(ID()); } 
private:
    uint32_t passIndex[3] = { 0, 0, 0};
    RDGPassExecuteFunc execute;
    uint32_t multiviewCount = 0;    

    friend class RDGRenderPassBuilder;
    friend class RDGBuilder;
};
using RDGRenderPassNodeRef = RDGRenderPassNode*;

class RDGComputePassNode : public RDGPassNode
{
public:
    RDGComputePassNode(std::string name) 
    : RDGPassNode(name, RDG_PASS_NODE_TYPE_COMPUTE) 
    {}

    RDGComputePassHandle GetHandle() { return RDGComputePassHandle(ID()); } 
private:
    uint32_t passIndex[3] = { 0, 0, 0};
    RDGPassExecuteFunc execute;

    friend class RDGComputePassBuilder;
    friend class RDGBuilder;
};
using RDGComputePassNodeRef = RDGComputePassNode*;

class RDGRayTracingPassNode : public RDGPassNode
{
public:
    RDGRayTracingPassNode(std::string name) 
    : RDGPassNode(name, RDG_PASS_NODE_TYPE_RAY_TRACING) 
    {}

    RDGRayTracingPassHandle GetHandle() { return RDGRayTracingPassHandle(ID()); } 
private:
    uint32_t passIndex[3] = { 0, 0, 0};
    RDGPassExecuteFunc execute;

    friend class RDGRayTracingPassBuilder;
    friend class RDGBuilder;
};
using RDGRayTracingPassNodeRef = RDGRayTracingPassNode*;

class RDGPresentPassNode : public RDGPassNode
{
public:
    RDGPresentPassNode(std::string name) 
    : RDGPassNode(name, RDG_PASS_NODE_TYPE_PRESENT) 
    {}

    RDGPresentPassHandle GetHandle() { return RDGPresentPassHandle(ID()); } 
private:
    friend class RDGPresentPassBuilder;
    friend class RDGBuilder;
};
using RDGPresentPassNodeRef = RDGPresentPassNode* ;

class RDGCopyPassNode : public RDGPassNode
{
public:
    RDGCopyPassNode(std::string name) 
    : RDGPassNode(name, RDG_PASS_NODE_TYPE_COPY) 
    {}

    RDGCopyPassHandle GetHandle() { return RDGCopyPassHandle(ID()); } 
    bool IsGenerateMip() { return generateMip; }
private:
    bool generateMip = false;

    friend class RDGCopyPassBuilder;
    friend class RDGBuilder;
};
using RDGCopyPassNodeRef = RDGCopyPassNode*;


// 依赖图/////////////////////////////////////////////////////////////////////////////////////

class RDGDependencyGraph
{
public:
    RDGDependencyGraph() { graph = DependencyGraph::Create(); }
    ~RDGDependencyGraph() { if (graph) DependencyGraph::Destroy(graph); }
    RDGDependencyGraph(const RDGDependencyGraph&) = delete;
    RDGDependencyGraph& operator=(const RDGDependencyGraph&) = delete;

    // 节点创建
    RDGTextureNodeRef CreateTextureNode(std::string name);
    RDGBufferNodeRef  CreateBufferNode(std::string name);
    RDGRenderPassNodeRef   CreateRenderPassNode(std::string name);
    RDGComputePassNodeRef  CreateComputePassNode(std::string name);
    RDGRayTracingPassNodeRef CreateRayTracingPassNode(std::string name);
    RDGPresentPassNodeRef  CreatePresentPassNode(std::string name);
    RDGCopyPassNodeRef     CreateCopyPassNode(std::string name);

    // 边连接
    void Link(RDGPassNodeRef from, RDGTextureNodeRef to, RDGTextureEdgeRef edge);
    void Link(RDGTextureNodeRef from, RDGPassNodeRef to, RDGTextureEdgeRef edge);
    void Link(RDGPassNodeRef from, RDGBufferNodeRef to, RDGBufferEdgeRef edge);
    void Link(RDGBufferNodeRef from, RDGPassNodeRef to, RDGBufferEdgeRef edge);

    // 类型化节点查询
    RDGTextureNodeRef GetTextureNode(DAGID id);
    RDGBufferNodeRef  GetBufferNode(DAGID id);
    RDGPassNodeRef    GetPassNode(DAGID id);

    uint32_t ResourceNodeCount()const { return resources.size(); }
    uint32_t PassNodeCount() const { return passes.size(); }

    uint32_t EdgeCount() { return graph->edge_count(); }

    void ForEachTextureNode(const std::function<void(RDGTextureNodeRef)>& func);
    void ForEachBufferNode(const std::function<void(RDGBufferNodeRef)>& func);
    void ForEachPassNode(const std::function<void(RDGPassNodeRef)>& func);

    void ForEachEdge(const std::function<void(DependencyGraphNode* from, DependencyGraphNode* to, DependencyGraphEdge* edge)>& func);

    // 遍历：遍历某资源关联的所有 pass
    void ForEachPass(RDGTextureNodeRef texture, const std::function<void(RDGTextureEdgeRef, RDGPassNodeRef)>& func);
    void ForEachPass(RDGBufferNodeRef buffer, const std::function<void(RDGBufferEdgeRef, RDGPassNodeRef)>& func);

    // 遍历：遍历某 pass 关联的所有资源
    void ForEachTexture(RDGPassNodeRef pass, const std::function<void(RDGTextureEdgeRef, RDGTextureNodeRef)>& func);
    void ForEachBuffer(RDGPassNodeRef pass, const std::function<void(RDGBufferEdgeRef, RDGBufferNodeRef)>& func);

    DependencyGraph* GetGraph() { return graph; }

private:
    friend struct IRenderGraphPhase;
    // 图的本体
    DependencyGraph* graph = nullptr;

    std::vector<RDGResourceNodeRef> resources;
    std::vector<RDGPassNodeRef> passes;
};
using RDGDependencyGraphRef = std::shared_ptr<RDGDependencyGraph>;
