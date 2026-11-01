#include "RDGBuilder.h"
#include "Core/DependencyGraph/DependencyGraph.h"
#include "Core/Log/Log.h"
#include "Core/Util/StringFormat.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGEdge.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RDG/RDGNode.h"
#include "Function/Render/RDG/RDGPool.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

RDGPassNodeRef RDGBlackBoard::Pass(std::string name)
{
    auto found = passes.find(name);
    if (found != passes.end()) {
        return found->second;
    }
    return nullptr;
}

RDGBufferNodeRef RDGBlackBoard::Buffer(std::string name)
{
    auto found = buffers.find(name);
    if (found != buffers.end()) {
        return found->second;
    }
    return nullptr;
}

RDGTextureNodeRef RDGBlackBoard::Texture(std::string name)
{
    auto found = textures.find(name);
    if (found != textures.end()) {
        return found->second;
    }
    return nullptr;
}

void RDGBlackBoard::AddPass(RDGPassNodeRef pass)
{
    passes[pass->Name()] = pass;
}

void RDGBlackBoard::AddBuffer(RDGBufferNodeRef buffer)
{
    buffers[buffer->Name()] = buffer;
}

void RDGBlackBoard::AddTexture(RDGTextureNodeRef texture)
{
    textures[texture->Name()] = texture;
}

RDGTextureBuilder RDGBuilder::CreateTexture(std::string name)
{
    if (blackBoard.Texture(name) != nullptr)
        ENGINE_LOG_WARN("Duplicate texture name [{}]: already registered in blackboard, please check name!", name.c_str());

    RDGTextureNodeRef textureNode = graph->CreateTextureNode(name);
    blackBoard.AddTexture(textureNode);
    return RDGTextureBuilder(this, textureNode);
}

RDGBufferBuilder RDGBuilder::CreateBuffer(std::string name)
{
    if (blackBoard.Buffer(name) != nullptr)
        ENGINE_LOG_WARN("Duplicate buffer name [{}]: already registered in blackboard, please check name!", name.c_str());

    RDGBufferNodeRef bufferNode = graph->CreateBufferNode(name);
    blackBoard.AddBuffer(bufferNode);
    return RDGBufferBuilder(this, bufferNode);
}

RDGTextureBuilder RDGBuilder::GetOrCreateTexture(std::string name)
{
    RDGTextureNodeRef textureNode = blackBoard.Texture(name);

    bool validationMode = true;
    if(textureNode == nullptr)
    {
        textureNode = graph->CreateTextureNode(name);
        blackBoard.AddTexture(textureNode);
        validationMode = false;
    }

    return RDGTextureBuilder(this, textureNode, validationMode);    // 漏传validationMode=复用节点时setter会覆写声明
}

RDGBufferBuilder RDGBuilder::GetOrCreateBuffer(std::string name)
{   
    RDGBufferNodeRef bufferNode = blackBoard.Buffer(name);

    bool validationMode = true;
    if(bufferNode == nullptr)
    {
        bufferNode = graph->CreateBufferNode(name);
        blackBoard.AddBuffer(bufferNode);
        validationMode = false;
    }
       
    return RDGBufferBuilder(this, bufferNode, validationMode);
}

RDGRenderPassBuilder RDGBuilder::CreateRenderPass(std::string name)
{
    if (blackBoard.Pass(name) != nullptr)
        ENGINE_LOG_WARN("Duplicate render pass name [{}]: already registered in blackboard, please check name!", name.c_str());

    RDGRenderPassNodeRef passNode = graph->CreateRenderPassNode(name);
    blackBoard.AddPass(passNode);
    return RDGRenderPassBuilder(this, passNode);
}

RDGComputePassBuilder RDGBuilder::CreateComputePass(std::string name)
{
    if (blackBoard.Pass(name) != nullptr)
        ENGINE_LOG_WARN("Duplicate compute pass name [{}]: already registered in blackboard, please check name!", name.c_str());

    RDGComputePassNodeRef passNode = graph->CreateComputePassNode(name);
    blackBoard.AddPass(passNode);
    return RDGComputePassBuilder(this, passNode);
}

RDGRayTracingPassBuilder RDGBuilder::CreateRayTracingPass(std::string name)
{
    if (blackBoard.Pass(name) != nullptr)
        ENGINE_LOG_WARN("Duplicate ray tracing pass name [{}]: already registered in blackboard, please check name!", name.c_str());

    RDGRayTracingPassNodeRef passNode = graph->CreateRayTracingPassNode(name);
    blackBoard.AddPass(passNode);
    return RDGRayTracingPassBuilder(this, passNode);
}

RDGPresentPassBuilder RDGBuilder::CreatePresentPass(std::string name)
{
    if (blackBoard.Pass(name) != nullptr)
        ENGINE_LOG_WARN("Duplicate present pass name [{}]: already registered in blackboard, please check name!", name.c_str());

    RDGPresentPassNodeRef passNode = graph->CreatePresentPassNode(name);
    blackBoard.AddPass(passNode);
    return RDGPresentPassBuilder(this, passNode);
}

RDGCopyPassBuilder RDGBuilder::CreateCopyPass(std::string name)
{
    if (blackBoard.Pass(name) != nullptr)
        ENGINE_LOG_WARN("Duplicate copy pass name [{}]: already registered in blackboard, please check name!", name.c_str());

    RDGCopyPassNodeRef passNode = graph->CreateCopyPassNode(name);
    blackBoard.AddPass(passNode);
    // passes.push_back(passNode);
    return RDGCopyPassBuilder(this, passNode);
}

RDGTextureHandle RDGBuilder::GetTexture(std::string name)
{
    auto node = blackBoard.Texture(name);
    if(node == nullptr) 
    {
        ENGINE_LOG_WARN("Unable to find RDG resource [{}], please check name!", name.c_str());
        return RDGTextureHandle(UINT64_MAX);
    }
    return node->GetHandle();
}

RDGBufferHandle RDGBuilder::GetBuffer(std::string name)
{
    auto node = blackBoard.Buffer(name);
    if(node == nullptr) 
    {
        ENGINE_LOG_WARN("Unable to find RDG resource [{}], please check name!", name.c_str());
        return RDGBufferHandle(UINT64_MAX);
    }
    return node->GetHandle();
}

// 布局动态记载制：见RDGBuilder.h的RDGTextureBuilder::Import注释——bornState仅首见兜底，
// registry记录（真实终态）存在时被覆盖；validationMode断言的是兜底值同源（多导入点一致性）
RDGTextureBuilder& RDGTextureBuilder::Import(RHITextureRef texture)
{
    return Import(texture, RESOURCE_STATE_UNDEFINED);
}

RDGTextureBuilder& RDGTextureBuilder::Import(RHITextureRef texture, RHIResourceState bornState)
{
    if(validationMode)
    {
        assert(this->texture->isImported == true);
        assert(this->texture->texture == texture);
        assert(this->texture->initState == bornState);
    }
    else
    {
        this->texture->isImported = true;
        this->texture->texture = texture;
        this->texture->info = texture->GetInfo();
        this->texture->initState = bornState;
    }

    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::Exetent(Extent3D extent)
{
    if(validationMode)
    {
        assert(texture->info.extent == extent);
    }
    else
    {
        texture->info.extent = extent;
    }

    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::Format(RHIFormat format)
{
    if(validationMode)
    {
        assert(texture->info.format == format);
    }
    else
    {
        texture->info.format = format;
    }

    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::MemoryUsage(enum MemoryUsage memoryUsage)
{
    if(validationMode)
    {
        assert(texture->info.memoryUsage == memoryUsage);
    }
    else
    {
        texture->info.memoryUsage = memoryUsage;
    }
    
    return  *this;
}

RDGTextureBuilder& RDGTextureBuilder::AllowReadWrite()
{
    if(validationMode)
    {
        assert(texture->info.type & RESOURCE_TYPE_RW_TEXTURE);
    }
    else
    {
        texture->info.type |= RESOURCE_TYPE_RW_TEXTURE;
    }

    return  *this;
}

RDGTextureBuilder& RDGTextureBuilder::AllowConcurrent(std::initializer_list<QueueType> families)
{
    assert(families.size() > 0 && families.size() <= QUEUE_TYPE_MAX_ENUM && "AllowConcurrent: 空族集或超QUEUE_TYPE_MAX_ENUM");
    if (validationMode)
    {
        assert(texture->info.sharingMode == RESOURCE_SHARING_TYPE_CONCURRENT);
        assert(texture->info.concurrentFamilyCount == families.size());
        uint32_t i = 0;
        for (QueueType t : families)
            assert(texture->info.concurrentFamilies[i++] == static_cast<uint32_t>(t) && "AllowConcurrent: 复用节点族集不一致");
    }
    else
    {
        texture->info.sharingMode = RESOURCE_SHARING_TYPE_CONCURRENT;
        texture->info.concurrentFamilies = {};
        texture->info.concurrentFamilyCount = 0;
        for (QueueType t : families)
            texture->info.concurrentFamilies[texture->info.concurrentFamilyCount++] = static_cast<uint32_t>(t);
    }

    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::AllowRenderTarget()
{
    if(validationMode)
    {
        assert(texture->info.type & RESOURCE_TYPE_RENDER_TARGET);
    }
    else
    {
        texture->info.type |= RESOURCE_TYPE_RENDER_TARGET;
    }

    return  *this;
}

RDGTextureBuilder& RDGTextureBuilder::AllowDepthStencil()
{
    if(validationMode)
    {
        assert(texture->info.type & RESOURCE_TYPE_RENDER_TARGET);
    }
    else
    {
        texture->info.type |= RESOURCE_TYPE_RENDER_TARGET;
    }

    return  *this;
}

RDGTextureBuilder& RDGTextureBuilder::MipLevels(uint32_t mipLevels)
{
    // 默认值(0=按extent推导)的展开已移交PassInfoAnalysis展开站——Builder只记声明
    if(validationMode)
    {
        assert(mipLevels == 0 || texture->info.mipLevels == mipLevels);
    }
    else
    {
        texture->info.mipLevels = mipLevels;
    }

    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::ArrayLayers(uint32_t arrayLayers)
{
    if(validationMode)
    {
        assert(texture->info.arrayLayers == arrayLayers);
    }
    else
    {
        texture->info.arrayLayers = arrayLayers;
    }
    
    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::Import(RHIBufferRef buffer, RHIResourceState initState, bool firstFrameUndefined)
{
    if (firstFrameUndefined && EngineContext::GetCurretTick() == 0) initState = RESOURCE_STATE_UNDEFINED;

    if(validationMode)
    {
        assert(this->buffer->isImported == true);
        assert(this->buffer->buffer == buffer);
        assert(this->buffer->initState == initState);
    }
    else 
    {
        this->buffer->isImported = true;
        this->buffer->buffer = buffer;
        this->buffer->info = buffer->GetInfo();
        this->buffer->initState = initState;
    }

    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::Size(uint32_t size)
{
    if(validationMode)
    {
        assert(buffer->info.size == size);
    }
    else
    {
        buffer->info.size = size;
    }

    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::MemoryUsage(enum MemoryUsage memoryUsage)
{
    if(validationMode)
    {
        assert(buffer->info.memoryUsage == memoryUsage);
    }
    else
    {
        buffer->info.memoryUsage = memoryUsage;
    }

    return  *this;
}

RDGBufferBuilder& RDGBufferBuilder::AllowVertexBuffer()
{
    if(validationMode)
    {
        assert(buffer->info.type & RESOURCE_TYPE_VERTEX_BUFFER);
    }
    else
    {
        buffer->info.type |= RESOURCE_TYPE_VERTEX_BUFFER;
    }

    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::AllowIndexBuffer()
{
    if(validationMode)
    {
        assert(buffer->info.type & RESOURCE_TYPE_INDEX_BUFFER);
    }
    else
    {
        buffer->info.type |= RESOURCE_TYPE_INDEX_BUFFER;
    }

    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::AllowReadWrite()
{
    if(validationMode)
    {
        assert(buffer->info.type & RESOURCE_TYPE_RW_BUFFER);
    }
    else
    {
        buffer->info.type |= RESOURCE_TYPE_RW_BUFFER;
    }

    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::AllowRead()
{
    if(validationMode)
    {
        assert(buffer->info.type & RESOURCE_TYPE_UNIFORM_BUFFER);
    }
    else
    {
        buffer->info.type |= RESOURCE_TYPE_UNIFORM_BUFFER;
    }

    return *this;    
}

// 未声明shaders/槽位不在反射条目中
static const ShaderResourceEntry* RequireSet2Entry(const std::vector<ShaderResourceEntry>& set2Entries, const std::string& passName, uint32_t set, uint32_t binding)
{
    for (const ShaderResourceEntry& e : set2Entries)
        if (e.set == set && e.binding == binding)
            return &e;
    std::string inventory;
    for (const ShaderResourceEntry& e : set2Entries)
        inventory += std::to_string(e.binding) + " ";
    LOG_FATAL("pass[%s]: descriptor edge slot (set=%u,binding=%u) not in .Shaders() reflected entries (shaders undeclared / missing edge / wrong slot macro). Actual entries [%zu]: %s",
        passName.c_str(), set, binding, set2Entries.size(), inventory.c_str());
    return nullptr;    // LOG_FATAL必抛,返回仅为控制流完备
}

// 反射type须属于该API允许的族
static void CheckEdgeFamily(const std::string& api, const std::string& passName, const ShaderResourceEntry& e, uint32_t allowedBits)
{
    if ((e.type & allowedBits) == 0)    // 反射type为单bit,位与为零=不在族内
        LOG_FATAL("pass[%s]: %s slot (set=%u,binding=%u) reflected type=0x%x not in allowed family 0x%x", passName.c_str(), api.c_str(), e.set, e.binding, e.type, allowedBits);
}

// 反射type必须按位包含于资源desc的info.type
static void CheckTypeContained(const std::string& api, const std::string& passName, const ShaderResourceEntry& e, uint32_t resourceInfoType)
{
    if ((e.type & resourceInfoType) != e.type)
        LOG_FATAL("pass[%s]: %s slot (set=%u,binding=%u) reflected type=0x%x not contained in resource info.type=0x%x (resource-side declaration too narrow)", passName.c_str(), api.c_str(), e.set, e.binding, e.type, resourceInfoType);
}

void AuditSet2ElementCoverage(RDGDependencyGraphRef graph, RDGPassNodeRef pass, const std::vector<ShaderResourceEntry>& entries, const std::string& passName)
{
    std::unordered_map<uint32_t, std::vector<uint32_t>> written;   // binding→已声明边的index集
    graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef) {
        if (edge->set == 2 && (edge->asShaderRead || edge->asShaderReadWrite))
            written[edge->binding].push_back(edge->index);
    });
    graph->ForEachBuffer(pass, [&](RDGBufferEdgeRef edge, RDGBufferNodeRef) {
        if (edge->set == 2 && (edge->asShaderRead || edge->asShaderReadWrite))
            written[edge->binding].push_back(edge->index);
    });

    static std::unordered_set<std::string> reported;   // 进程内去重
    for (const ShaderResourceEntry& e : entries)
    {
        // size==0=shader侧单块/无维度声明（如INDIRECT_MESH_DRAW_DATAS——RS侧按数组用,shader侧
        // 声明单块,反射报0）——元素粒度对账无据可依,整条跳过（哨兵侧同因天然免疫）
        if (e.size == 0) continue;

        const std::vector<uint32_t>& indices = written[e.binding];   // 无键=空
        for (uint32_t idx : indices)
            if (idx >= e.size)
                LOG_FATAL("pass[%s]: set2 b%u[%u] out-of-range write: edge index >= declared count=%u (deterministic error)", passName.c_str(), e.binding, idx, e.size);
        for (uint32_t i = 0; i < e.size; i++)
        {
            bool covered = false;
            for (uint32_t idx : indices) 
                if (idx == i) { covered = true; break; }
            if (covered) continue;
            std::string key = passName + ":" + std::to_string(e.binding) + ":" + std::to_string(i);
            if (reported.insert(std::move(key)).second)
                ENGINE_LOG_WARN("[Set2Gap] pass[{}] set2 b{}[{}]: declared count={} with no covering edge (Finish gate element granularity, cross-checked with [DanglingSlot] sentinel)",
                    passName.c_str(), e.binding, i, e.size);
        }
    }
}

RDGRenderPassBuilder& RDGRenderPassBuilder::PassIndex(uint32_t x, uint32_t y, uint32_t z)
{
    pass->passIndex[0] = x;
    pass->passIndex[1] = y;
    pass->passIndex[2] = z;
    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::RootSignature(RHIRootSignatureRef rootSignature)
{
    pass->rootSignature = rootSignature;
    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::Shaders(std::initializer_list<RHIShaderRef> shaders)
{
    // 双重声明守卫:二次调用会静默重置条目数组,直接致命
    if (pass->shadersDeclared) LOG_FATAL("pass[%s]: .Shaders() called twice", pass->Name().c_str());
    // 边后调用守卫:新边API已用后再声明会静默重置done标记,同样致命(R9)
    pass->shadersDeclared = true;

    pass->set2Entries.clear();
    for (const RHIShaderRef& shader : shaders)
        for (const ShaderResourceEntry& e : shader->GetReflectInfo().resources)
        {
            if (!e.used || e.set != 2 || e.type == RESOURCE_TYPE_SAMPLER) continue;
            // 并集去重:同(set,binding)合并frequency/取max size;类型冲突=两阶段同槽不同族,致命
            ShaderResourceEntry* exist = nullptr;
            for (ShaderResourceEntry& candidate : pass->set2Entries)
                if (candidate.set == e.set && candidate.binding == e.binding) { exist = &candidate; break; }
            if (exist)
            {
                if (exist->type != e.type)
                    LOG_FATAL("pass[%s]: set2 slot (set=%u,binding=%u) type conflict 0x%x vs 0x%x", pass->Name().c_str(), e.set, e.binding, exist->type, e.type);
                exist->frequency |= e.frequency;
                exist->size = std::max(exist->size, e.size);
            }
            else
                pass->set2Entries.push_back(e);
        }
    std::sort(pass->set2Entries.begin(), pass->set2Entries.end(), [](const ShaderResourceEntry& a, const ShaderResourceEntry& b)
    {
        if (a.set != b.set) return a.set < b.set;
        return a.binding < b.binding;
    });
    return *this;
}

// ===== S3:反射驱动描述符边(type/frequency由.Shaders()反射条目回填,取代调用方手填;
RDGRenderPassBuilder& RDGRenderPassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
    RDGTextureNodeRef node = graph->GetTextureNode(texture.ID());
    const ShaderResourceEntry& e = *RequireSet2Entry(pass->set2Entries, pass->Name(), set, binding);
    CheckEdgeFamily("Read(tex)", pass->Name(), e, RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_TEXTURE_CUBE | RESOURCE_TYPE_COMBINED_IMAGE_SAMPLER);
    CheckTypeContained("Read(tex)", pass->Name(), e, node->GetInfo().type);

    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->viewType = viewType;

    edge->type = e.type;
    edge->frequency = e.frequency;

    graph->Link(node, pass, edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
    RDGTextureNodeRef node = graph->GetTextureNode(texture.ID());
    const ShaderResourceEntry& e = *RequireSet2Entry(pass->set2Entries, pass->Name(), set, binding);
    CheckEdgeFamily("ReadWrite(tex)", pass->Name(), e, RESOURCE_TYPE_RW_TEXTURE);
    CheckTypeContained("ReadWrite(tex)", pass->Name(), e, node->GetInfo().type);

    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->asShaderReadWrite = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->viewType = viewType;

    edge->type = e.type;
    edge->frequency = e.frequency;

    graph->Link(pass, node, edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferNodeRef node = graph->GetBufferNode(buffer.ID());
    const ShaderResourceEntry& e = *RequireSet2Entry(pass->set2Entries, pass->Name(), set, binding);
    CheckEdgeFamily("Read(buf)", pass->Name(), e, RESOURCE_TYPE_UNIFORM_BUFFER | RESOURCE_TYPE_RW_BUFFER | RESOURCE_TYPE_TEXEL_BUFFER);
    CheckTypeContained("Read(buf)", pass->Name(), e, node->GetInfo().type);

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;

    edge->type = e.type;
    edge->frequency = e.frequency;

    graph->Link(node, pass, edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferNodeRef node = graph->GetBufferNode(buffer.ID());
    const ShaderResourceEntry& e = *RequireSet2Entry(pass->set2Entries, pass->Name(), set, binding);
    CheckEdgeFamily("ReadWrite(buf)", pass->Name(), e, RESOURCE_TYPE_RW_BUFFER | RESOURCE_TYPE_RW_TEXEL_BUFFER);
    CheckTypeContained("ReadWrite(buf)", pass->Name(), e, node->GetInfo().type);

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderReadWrite = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;

    edge->type = e.type;
    edge->frequency = e.frequency;

    graph->Link(pass, node, edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::Color(  uint32_t binding, RDGTextureHandle texture,
                                                    AttachmentLoadOp load, 
                                                    AttachmentStoreOp store, 
                                                    Color4 clearColor, 
                                                    TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_COLOR_ATTACHMENT;
    edge->loadOp = load;
    edge->storeOp = store;
    edge->clearColor = clearColor;
    edge->subresource = subresource;
    edge->asColor = true;
    edge->binding = binding;
    edge->viewType = subresource.layerCount > 1 ? VIEW_TYPE_2D_ARRAY : VIEW_TYPE_2D;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}   

RDGRenderPassBuilder& RDGRenderPassBuilder::DepthStencil(   RDGTextureHandle texture, 
                                                            AttachmentLoadOp load, 
                                                            AttachmentStoreOp store, 
                                                            float clearDepth,
                                                            uint32_t clearStencil,
                                                            TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_DEPTH_STENCIL_ATTACHMENT;
    edge->loadOp = load;
    edge->storeOp = store;
    edge->clearDepth = clearDepth;
    edge->clearStencil = clearStencil;
    edge->subresource = subresource;
    edge->asDepthStencil = true;
    edge->viewType = subresource.layerCount > 1 ? VIEW_TYPE_2D_ARRAY : VIEW_TYPE_2D;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::Multiview(uint32_t multiviewCount)
{
    pass->multiviewCount = multiviewCount;

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::Read(RDGBufferHandle buffer)
{
    if (buffer.ID() == UINT64_MAX) return *this;    

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->set = NO_DESCRIPTOR_SET;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::Read(RDGTextureHandle texture)
{
    if (texture.ID() == UINT64_MAX) return *this;    

    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = {};
    edge->asShaderRead = true;
    edge->set = NO_DESCRIPTOR_SET;
    edge->binding = 0;
    edge->index = 0;
    edge->type = RESOURCE_TYPE_TEXTURE;
    edge->viewType = VIEW_TYPE_2D;

    graph->Link(graph->GetTextureNode(texture.ID()), pass, edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::ReadIndirect(RDGBufferHandle buffer)
{
    if (buffer.ID() == UINT64_MAX) return *this;    

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_INDIRECT_ARGUMENT;
    edge->set = NO_DESCRIPTOR_SET;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::ReadAccelerationStructure(RDGBufferHandle buffer)
{
    if (buffer.ID() == UINT64_MAX) return *this;    

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_ACCELERATION_STRUCTURE;
    edge->set = NO_DESCRIPTOR_SET;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::Execute(const RDGPassExecuteFunc& execute)
{
    pass->execute = execute;
    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::AddFlag(const RDGPassFlags flag)
{
    pass->add_flags(flag);
    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::PassIndex(uint32_t x, uint32_t y, uint32_t z)
{
    pass->passIndex[0] = x;
    pass->passIndex[1] = y;
    pass->passIndex[2] = z;
    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::RootSignature(RHIRootSignatureRef rootSignature)
{
    pass->rootSignature = rootSignature;
    return *this;
}   

RDGComputePassBuilder& RDGComputePassBuilder::Shaders(std::initializer_list<RHIShaderRef> shaders)
{
    // 双重声明守卫:二次调用会静默重置条目数组,直接致命
    if (pass->shadersDeclared) LOG_FATAL("pass[%s]: .Shaders() called twice", pass->Name().c_str());
    // 边后调用守卫:新边API已用后再声明会静默重置done标记,同样致命(R9)
    pass->shadersDeclared = true;

    pass->set2Entries.clear();
    for (const RHIShaderRef& shader : shaders)
        for (const ShaderResourceEntry& e : shader->GetReflectInfo().resources)
        {
            if (!e.used || e.set != 2 || e.type == RESOURCE_TYPE_SAMPLER) continue;
            // 并集去重:同(set,binding)合并frequency/取max size;类型冲突=两阶段同槽不同族,致命
            ShaderResourceEntry* exist = nullptr;
            for (ShaderResourceEntry& candidate : pass->set2Entries)
                if (candidate.set == e.set && candidate.binding == e.binding) { exist = &candidate; break; }
            if (exist)
            {
                if (exist->type != e.type)
                    LOG_FATAL("pass[%s]: set2 slot (set=%u,binding=%u) type conflict 0x%x vs 0x%x", pass->Name().c_str(), e.set, e.binding, exist->type, e.type);
                exist->frequency |= e.frequency;
                exist->size = std::max(exist->size, e.size);
            }
            else
                pass->set2Entries.push_back(e);
        }
    std::sort(pass->set2Entries.begin(), pass->set2Entries.end(), [](const ShaderResourceEntry& a, const ShaderResourceEntry& b)
    {
        if (a.set != b.set) return a.set < b.set;
        return a.binding < b.binding;
    });
    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::ReadBuffer(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size, ResourceType type)
{
    if (set == 2) LOG_FATAL("pass[%s]: set2 edges must use Read/ReadWrite (reflection family short names); this is the legacy slotted family (pre-swap Read), reserved for NRD set0/1 dynamic binding", pass->Name().c_str());
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = type;      // 同render版：须与根签名条目一致

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}


RDGComputePassBuilder& RDGComputePassBuilder::ReadTexture(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
    if (set == 2) LOG_FATAL("pass[%s]: set2 edges must use Read/ReadWrite (reflection family short names); this is the legacy slotted family (pre-swap Read), reserved for NRD set0/1 dynamic binding", pass->Name().c_str());
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_TEXTURE;
    edge->viewType = viewType;

    graph->Link(graph->GetTextureNode(texture.ID()), pass, edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::Read(RDGBufferHandle buffer)
{
    if (buffer.ID() == UINT64_MAX) return *this;    

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->set = NO_DESCRIPTOR_SET;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::Read(RDGTextureHandle texture)
{
    if (texture.ID() == UINT64_MAX) return *this;    

    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = {};
    edge->asShaderRead = true;
    edge->set = NO_DESCRIPTOR_SET;
    edge->binding = 0;
    edge->index = 0;
    edge->type = RESOURCE_TYPE_TEXTURE;
    edge->viewType = VIEW_TYPE_2D;

    graph->Link(graph->GetTextureNode(texture.ID()), pass, edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::ReadIndirect(RDGBufferHandle buffer)
{
    if (buffer.ID() == UINT64_MAX) return *this;    

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_INDIRECT_ARGUMENT;
    edge->set = NO_DESCRIPTOR_SET;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::ReadAccelerationStructure(RDGBufferHandle buffer)
{
    if (buffer.ID() == UINT64_MAX) return *this;    

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_ACCELERATION_STRUCTURE;
    edge->set = NO_DESCRIPTOR_SET;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::ReadWriteTexture(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
    if (set == 2) LOG_FATAL("pass[%s]: set2 edges must use Read/ReadWrite (reflection family short names); this is the legacy slotted family (pre-swap ReadWrite), reserved for NRD set0/1 dynamic binding", pass->Name().c_str());
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->asShaderReadWrite = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_RW_TEXTURE;
    edge->viewType = viewType;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
    RDGTextureNodeRef node = graph->GetTextureNode(texture.ID());
    const ShaderResourceEntry& e = *RequireSet2Entry(pass->set2Entries, pass->Name(), set, binding);
    CheckEdgeFamily("Read(tex)", pass->Name(), e, RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_TEXTURE_CUBE | RESOURCE_TYPE_COMBINED_IMAGE_SAMPLER);
    CheckTypeContained("Read(tex)", pass->Name(), e, node->GetInfo().type);

    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->viewType = viewType;

    edge->type = e.type;
    edge->frequency = e.frequency;

    graph->Link(node, pass, edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
    RDGTextureNodeRef node = graph->GetTextureNode(texture.ID());
    const ShaderResourceEntry& e = *RequireSet2Entry(pass->set2Entries, pass->Name(), set, binding);
    CheckEdgeFamily("ReadWrite(tex)", pass->Name(), e, RESOURCE_TYPE_RW_TEXTURE);
    CheckTypeContained("ReadWrite(tex)", pass->Name(), e, node->GetInfo().type);

    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->asShaderReadWrite = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->viewType = viewType;

    edge->type = e.type;
    edge->frequency = e.frequency;

    graph->Link(pass, node, edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferNodeRef node = graph->GetBufferNode(buffer.ID());
    const ShaderResourceEntry& e = *RequireSet2Entry(pass->set2Entries, pass->Name(), set, binding);
    CheckEdgeFamily("Read(buf)", pass->Name(), e, RESOURCE_TYPE_UNIFORM_BUFFER | RESOURCE_TYPE_RW_BUFFER | RESOURCE_TYPE_TEXEL_BUFFER);
    CheckTypeContained("Read(buf)", pass->Name(), e, node->GetInfo().type);

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;

    edge->type = e.type;
    edge->frequency = e.frequency;

    graph->Link(node, pass, edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferNodeRef node = graph->GetBufferNode(buffer.ID());
    const ShaderResourceEntry& e = *RequireSet2Entry(pass->set2Entries, pass->Name(), set, binding);
    CheckEdgeFamily("ReadWrite(buf)", pass->Name(), e, RESOURCE_TYPE_RW_BUFFER | RESOURCE_TYPE_RW_TEXEL_BUFFER);
    CheckTypeContained("ReadWrite(buf)", pass->Name(), e, node->GetInfo().type);

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderReadWrite = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;

    edge->type = e.type;
    edge->frequency = e.frequency;

    graph->Link(pass, node, edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::AddFlag(const RDGPassFlags flag)
{
    pass->add_flags(flag);
    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::Execute(const RDGPassExecuteFunc& execute)
{
    pass->execute = execute;
    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::PassIndex(uint32_t x, uint32_t y, uint32_t z)
{
    pass->passIndex[0] = x;
    pass->passIndex[1] = y;
    pass->passIndex[2] = z;
    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::RootSignature(RHIRootSignatureRef rootSignature)
{
    pass->rootSignature = rootSignature;
    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::Shaders(std::initializer_list<RHIShaderRef> shaders)
{
    // 双重声明守卫
    if (pass->shadersDeclared) LOG_FATAL("pass[%s]: .Shaders() called twice", pass->Name().c_str());
    // 边后调用守卫
    pass->shadersDeclared = true;

    pass->set2Entries.clear();
    for (const RHIShaderRef& shader : shaders)
        for (const ShaderResourceEntry& e : shader->GetReflectInfo().resources)
        {
            if (!e.used || e.set != 2 || e.type == RESOURCE_TYPE_SAMPLER) continue;
            // 并集去重:同(set,binding)合并frequency/取max size;类型冲突=两阶段同槽不同族,致命
            ShaderResourceEntry* exist = nullptr;
            for (ShaderResourceEntry& candidate : pass->set2Entries)
                if (candidate.set == e.set && candidate.binding == e.binding) { exist = &candidate; break; }
            if (exist)
            {
                if (exist->type != e.type)
                    LOG_FATAL("pass[%s]: set2 slot (set=%u,binding=%u) type conflict 0x%x vs 0x%x", pass->Name().c_str(), e.set, e.binding, exist->type, e.type);
                exist->frequency |= e.frequency;
                exist->size = std::max(exist->size, e.size);
            }
            else
                pass->set2Entries.push_back(e);
        }
    std::sort(pass->set2Entries.begin(), pass->set2Entries.end(), [](const ShaderResourceEntry& a, const ShaderResourceEntry& b)
    {
        if (a.set != b.set) return a.set < b.set;
        return a.binding < b.binding;
    });
    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
    RDGTextureNodeRef node = graph->GetTextureNode(texture.ID());
    const ShaderResourceEntry& e = *RequireSet2Entry(pass->set2Entries, pass->Name(), set, binding);
    CheckEdgeFamily("Read(tex)", pass->Name(), e, RESOURCE_TYPE_TEXTURE | RESOURCE_TYPE_TEXTURE_CUBE | RESOURCE_TYPE_COMBINED_IMAGE_SAMPLER);
    CheckTypeContained("Read(tex)", pass->Name(), e, node->GetInfo().type);

    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->viewType = viewType;

    edge->type = e.type;
    edge->frequency = e.frequency;

    graph->Link(node, pass, edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
    RDGTextureNodeRef node = graph->GetTextureNode(texture.ID());
    const ShaderResourceEntry& e = *RequireSet2Entry(pass->set2Entries, pass->Name(), set, binding);
    CheckEdgeFamily("ReadWrite(tex)", pass->Name(), e, RESOURCE_TYPE_RW_TEXTURE);
    CheckTypeContained("ReadWrite(tex)", pass->Name(), e, node->GetInfo().type);

    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->asShaderReadWrite = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->viewType = viewType;

    edge->type = e.type;
    edge->frequency = e.frequency;

    graph->Link(pass, node, edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferNodeRef node = graph->GetBufferNode(buffer.ID());
    const ShaderResourceEntry& e = *RequireSet2Entry(pass->set2Entries, pass->Name(), set, binding);
    CheckEdgeFamily("Read(buf)", pass->Name(), e, RESOURCE_TYPE_UNIFORM_BUFFER | RESOURCE_TYPE_RW_BUFFER | RESOURCE_TYPE_TEXEL_BUFFER);
    CheckTypeContained("Read(buf)", pass->Name(), e, node->GetInfo().type);

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;

    edge->type = e.type;
    edge->frequency = e.frequency;

    graph->Link(node, pass, edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferNodeRef node = graph->GetBufferNode(buffer.ID());
    const ShaderResourceEntry& e = *RequireSet2Entry(pass->set2Entries, pass->Name(), set, binding);
    CheckEdgeFamily("ReadWrite(buf)", pass->Name(), e, RESOURCE_TYPE_RW_BUFFER | RESOURCE_TYPE_RW_TEXEL_BUFFER);
    CheckTypeContained("ReadWrite(buf)", pass->Name(), e, node->GetInfo().type);

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderReadWrite = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;

    edge->type = e.type;
    edge->frequency = e.frequency;

    graph->Link(pass, node, edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::Read(RDGBufferHandle buffer)
{
    if (buffer.ID() == UINT64_MAX) return *this;

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->set = NO_DESCRIPTOR_SET;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::Write(RDGBufferHandle buffer, RHIResourceState state)
{
    if (buffer.ID() == UINT64_MAX) return *this;

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = state;
    edge->asShaderReadWrite = true;     // 借write记账语义进sync模型；NO_DESCRIPTOR_SET=全线无描述符副作用
    edge->set = NO_DESCRIPTOR_SET;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::Read(RDGTextureHandle texture)
{
    if (texture.ID() == UINT64_MAX) return *this;    

    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = {};
    edge->asShaderRead = true;
    edge->set = NO_DESCRIPTOR_SET;
    edge->binding = 0;
    edge->index = 0;
    edge->type = RESOURCE_TYPE_TEXTURE;
    edge->viewType = VIEW_TYPE_2D;

    graph->Link(graph->GetTextureNode(texture.ID()), pass, edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::ReadIndirect(RDGBufferHandle buffer)
{
    if (buffer.ID() == UINT64_MAX) return *this;    

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_INDIRECT_ARGUMENT;
    edge->set = NO_DESCRIPTOR_SET;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::ReadAccelerationStructure(RDGBufferHandle buffer)
{
    if (buffer.ID() == UINT64_MAX) return *this;    

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_ACCELERATION_STRUCTURE;
    edge->set = NO_DESCRIPTOR_SET;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::AddFlag(const RDGPassFlags flag)
{
    pass->add_flags(flag);
    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::Execute(const RDGPassExecuteFunc& execute)
{
    pass->execute = execute;
    return *this;
}

RDGPresentPassBuilder& RDGPresentPassBuilder::Texture(RDGTextureHandle texture, TextureSubresourceLayers subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_TRANSFER_SRC;
    // API收Layers（单mip语义），存入统一转Range（levelCount=1冻结；mip0真值保留——baseMipLevel=0合法）
    edge->subresource = { subresource.aspect, subresource.mipLevel, 1, subresource.baseArrayLayer, subresource.layerCount };

    graph->Link(graph->GetTextureNode(texture.ID()), pass, edge);

    return *this;
}

RDGPresentPassBuilder& RDGPresentPassBuilder::PresentTexture(RDGTextureHandle texture)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_PRESENT;
    edge->asPresent = true;

    graph->Link(graph->GetTextureNode(texture.ID()), pass, edge);

    return *this;    
}

RDGCopyPassBuilder& RDGCopyPassBuilder::From(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_TRANSFER_SRC;
    edge->offset = offset;
    edge->size = size;
    edge->asTransferSrc = true;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::From(RDGTextureHandle texture, TextureSubresourceLayers subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_TRANSFER_SRC;
    edge->subresource = { subresource.aspect, subresource.mipLevel, 1, subresource.baseArrayLayer, subresource.layerCount };
    edge->asTransferSrc = true;

    graph->Link(graph->GetTextureNode(texture.ID()), pass, edge);

    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::To(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_TRANSFER_DST;
    edge->offset = offset;
    edge->size = size;
    edge->asTransferDst = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::To(RDGTextureHandle texture, TextureSubresourceLayers subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_TRANSFER_DST;
    edge->subresource = { subresource.aspect, subresource.mipLevel, 1, subresource.baseArrayLayer, subresource.layerCount };
    edge->asTransferDst = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::GenerateMips()
{
    pass->generateMip = true;
    return *this;   
}

RDGCopyPassBuilder& RDGCopyPassBuilder::AddFlag(const RDGPassFlags flag)
{
    pass->add_flags(flag);
    return *this;
}


