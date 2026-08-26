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
    RDGTextureNodeRef textureNode = graph->CreateTextureNode(name);
    blackBoard.AddTexture(textureNode);
    return RDGTextureBuilder(this, textureNode);
}

RDGBufferBuilder RDGBuilder::CreateBuffer(std::string name)
{
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
    
    return RDGTextureBuilder(this, textureNode);
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
    RDGRenderPassNodeRef passNode = graph->CreateRenderPassNode(name);
    blackBoard.AddPass(passNode);
    // passes.push_back(passNode);
    return RDGRenderPassBuilder(this, passNode);
}

RDGComputePassBuilder RDGBuilder::CreateComputePass(std::string name)
{
    RDGComputePassNodeRef passNode = graph->CreateComputePassNode(name);
    blackBoard.AddPass(passNode);
    // passes.push_back(passNode);
    return RDGComputePassBuilder(this, passNode);
}   

RDGRayTracingPassBuilder RDGBuilder::CreateRayTracingPass(std::string name)
{
    RDGRayTracingPassNodeRef passNode = graph->CreateRayTracingPassNode(name);
    blackBoard.AddPass(passNode);
    // passes.push_back(passNode);
    return RDGRayTracingPassBuilder(this, passNode);
}   

RDGPresentPassBuilder RDGBuilder::CreatePresentPass(std::string name)
{
    RDGPresentPassNodeRef passNode = graph->CreatePresentPassNode(name);
    blackBoard.AddPass(passNode);
    // passes.push_back(passNode);
    return RDGPresentPassBuilder(this, passNode);
}

RDGCopyPassBuilder RDGBuilder::CreateCopyPass(std::string name)
{
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

RDGTextureBuilder& RDGTextureBuilder::Import(RHITextureRef texture, RHIResourceState initState)
{
    if(validationMode)
    {
        assert(this->texture->isImported == true);
        assert(this->texture->texture == texture);
        assert(this->texture->initState == initState);
    }
    else
    {
        this->texture->isImported = true;
        this->texture->texture = texture;
        this->texture->info = texture->GetInfo();
        this->texture->initState = initState;
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
    if(validationMode)
    {
        assert(texture->info.mipLevels = mipLevels);
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

RDGBufferBuilder& RDGBufferBuilder::Import(RHIBufferRef buffer, RHIResourceState initState)
{
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

RDGRenderPassBuilder& RDGRenderPassBuilder::DescriptorSet(uint32_t set, RHIDescriptorSetRef descriptorSet)
{
    pass->descriptorSets[set] = descriptorSet;
    return *this;  
}

RDGRenderPassBuilder& RDGRenderPassBuilder::Sampler(uint32_t set, uint32_t binding, uint32_t index, RHISamplerRef sampler)
{
    pass->samplers.push_back({
        .sampler = sampler,
        .set = set,
        .binding = binding,
        .index = index
    });
    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_UNIFORM_BUFFER;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
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

RDGRenderPassBuilder& RDGRenderPassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderReadWrite = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_RW_BUFFER;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
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

RDGRenderPassBuilder& RDGRenderPassBuilder::OutputRead(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputRead = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::OutputRead(RDGTextureHandle texture, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::OutputReadWrite(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::OutputReadWrite(RDGTextureHandle texture, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

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

RDGComputePassBuilder& RDGComputePassBuilder::DescriptorSet(uint32_t set, RHIDescriptorSetRef descriptorSet)
{
    pass->descriptorSets[set] = descriptorSet;
    return *this;  
}  

RDGComputePassBuilder& RDGComputePassBuilder::Sampler(uint32_t set, uint32_t binding, uint32_t index, RHISamplerRef sampler)
{
    pass->samplers.push_back({
        .sampler = sampler,
        .set = set,
        .binding = binding,
        .index = index
    });
    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_UNIFORM_BUFFER;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
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

RDGComputePassBuilder& RDGComputePassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderReadWrite = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_RW_BUFFER;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
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

RDGComputePassBuilder& RDGComputePassBuilder::OutputRead(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputRead = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::OutputRead(RDGTextureHandle texture, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::OutputReadWrite(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::OutputReadWrite(RDGTextureHandle texture, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::OutputIndirectDraw(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_INDIRECT_ARGUMENT;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputIndirectDraw = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

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

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::DescriptorSet(uint32_t set, RHIDescriptorSetRef descriptorSet)
{
    pass->descriptorSets[set] = descriptorSet;
    return *this;  
}  

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::Sampler(uint32_t set, uint32_t binding, uint32_t index, RHISamplerRef sampler)
{
    pass->samplers.push_back({
        .sampler = sampler,
        .set = set,
        .binding = binding,
        .index = index
    });
    return *this; 
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_UNIFORM_BUFFER;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
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

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderReadWrite = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_RW_BUFFER;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
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

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::OutputRead(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputRead = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::OutputRead(RDGTextureHandle texture, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::OutputReadWrite(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::OutputReadWrite(RDGTextureHandle texture, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::Dependency(RDGBufferHandle buffer)
{
    // 无描述符的读向依赖边：不参与描述符绑定（绑定阶段按NO_DESCRIPTOR_SET跳过），
    // 但进入依赖分析与屏障跟踪——拓扑排序据此保证生产者pass（如TLAS Update）先于本pass
    if (buffer.ID() == UINT64_MAX) return *this;    // 生产者pass未构建（被禁用等），GetBuffer已警告

    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
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
    edge->subresourceLayer = subresource;

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
    edge->subresourceLayer = subresource;
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
    edge->subresourceLayer = subresource;
    edge->asTransferDst = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;    
}

RDGCopyPassBuilder& RDGCopyPassBuilder::GenerateMips()
{
    pass->generateMip = true;
    return *this;   
}

RDGCopyPassBuilder& RDGCopyPassBuilder::OutputRead(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputRead = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::OutputRead(RDGTextureHandle texture, TextureSubresourceLayers subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresourceLayer = subresource;
    edge->asOutputRead = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this; 
}

RDGCopyPassBuilder& RDGCopyPassBuilder::OutputReadWrite(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::OutputReadWrite(RDGTextureHandle texture, TextureSubresourceLayers subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresourceLayer = subresource;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this; 
}

RDGCopyPassBuilder& RDGCopyPassBuilder::AddFlag(const RDGPassFlags flag)
{
    pass->add_flags(flag);
    return *this;
}


