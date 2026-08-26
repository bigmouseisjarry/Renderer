#pragma once

#include "Core/DependencyGraph/DependencyGraph.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <cstdint>

// 所有RDG边均为 { 资源节点, pass节点 }
// 在node里存储各个资源和pass的基本信息，在edge内存储依赖信息（例如subresource，读写参数信息，描述符绑定信息等）
// sakura用了继承的edge子类来区分各种不同的资源依赖

// 简而言之 
// 资源 → Pass    Pass 读取该资源          Link(resource, pass, edge)
// Pass → 资源    Pass 写入/产出该资源     Link(pass, resource, edge)

enum RDGEdgeType
{
    RDG_EDGE_TYPE_TEXTURE = 0,
    RDG_EDGE_TYPE_BUFFER,

    RDG_EDGE_TYPE_MAX_ENUM,    //
};

// 虚拟依赖边的set哨兵：该边只表达排序/屏障依赖，不参与描述符绑定
// （绑定阶段见此set直接跳过该边）。用于如图外全局资源（TLAS等）的图内依赖表达
constexpr uint32_t NO_DESCRIPTOR_SET = UINT32_MAX;

class RDGEdge : public DependencyGraphEdge    
{
public:
    RDGEdge(RDGEdgeType edgeType, RHIResourceState state = RESOURCE_STATE_UNDEFINED)
    : state(state)
    , edgeType(edgeType)
    {}

	// 边相连的pass节点对边相连的资源节点做了产出操作并且该资源节点还会被后续的pass节点使用。
    // 则为输出边，返回true。
    virtual bool IsOutput() { return false; }

    RDGEdgeType EdgeType() { return edgeType; }

    RHIResourceState state; // 在对应的pass处要求的状态（若作为pass输入，pass不应在内部改变状态）

protected:
    RDGEdgeType edgeType;
};
using RDGEdgeRef = RDGEdge*;


class RDGTextureEdge : public RDGEdge
{
public:
    RDGTextureEdge() 
    : RDGEdge(RDG_EDGE_TYPE_TEXTURE) 
    {}

    TextureSubresourceRange subresource = {};
    TextureSubresourceLayers subresourceLayer = {};
    bool asColor = false;
    bool asDepthStencil = false;
    bool asShaderRead = false;
    bool asShaderReadWrite = false;
    bool asOutputRead = false;
    bool asOutputReadWrite = false;
    bool asPresent = false;
    bool asTransferSrc = false;
    bool asTransferDst = false;

    virtual bool IsOutput() override { return asOutputRead || asOutputReadWrite; }

    uint32_t set = 0;       // 描述符使用
    uint32_t binding = 0;   // 描述符/color attachment使用
    uint32_t index = 0;
    ResourceType type = RESOURCE_TYPE_TEXTURE;
    TextureViewType viewType = VIEW_TYPE_2D;

    AttachmentLoadOp 	loadOp          = ATTACHMENT_LOAD_OP_DONT_CARE;     // 仅attachments时使用
	AttachmentStoreOp	storeOp			= ATTACHMENT_STORE_OP_DONT_CARE;

	Color4				clearColor		= {0.0f, 0.0f, 0.0f, 0.0f};
	float				clearDepth 		= 1.0f;
	uint32_t			clearStencil 	= 0;
};
using RDGTextureEdgeRef = RDGTextureEdge*;


class RDGBufferEdge : public RDGEdge
{
public:
    RDGBufferEdge() 
    : RDGEdge(RDG_EDGE_TYPE_BUFFER) 
    {}

    uint32_t offset = 0;
    uint32_t size = 0;
    bool asShaderRead = false;
    bool asShaderReadWrite = false;
    bool asOutputRead = false;
    bool asOutputReadWrite = false;
    bool asOutputIndirectDraw = false;
    bool asTransferSrc = false;
    bool asTransferDst = false;

    virtual bool IsOutput() override { return asOutputRead || asOutputReadWrite || asOutputIndirectDraw; }

    uint32_t set = 0;       // 描述符使用
    uint32_t binding = 0;
    uint32_t index = 0;
    ResourceType type = RESOURCE_TYPE_UNIFORM_BUFFER;
};
using RDGBufferEdgeRef = RDGBufferEdge* ;