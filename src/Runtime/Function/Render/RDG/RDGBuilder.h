#pragma once

#include "Core/DependencyGraph/DependencyGraph.h"
#include "Core/Log/Log.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RDG/RDGNode.h"
#include "Function/Render/RHI/RHICommandList.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "RDGNode.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class RDGBlackBoard
{
public:
    RDGPassNodeRef Pass(std::string name);
    RDGBufferNodeRef Buffer(std::string name);
    RDGTextureNodeRef Texture(std::string name);

    void AddPass(RDGPassNodeRef pass);
    void AddBuffer(RDGBufferNodeRef buffer);
    void AddTexture(RDGTextureNodeRef texture);

    void Clear() { passes.clear(); buffers.clear(); textures.clear(); }

private:
    std::unordered_map<std::string, RDGPassNodeRef> passes;
    std::unordered_map<std::string, RDGBufferNodeRef> buffers;
    std::unordered_map<std::string, RDGTextureNodeRef> textures;
};

// UE中的RDG：
// 每个pass一个cpp文件 有graphbuilder的构建回调函数，
// meshpass继承pass，多一个获取场景meshbatch的回调函数
// RDG只有单帧生命周期，其分配的资源（返回引用句柄）也只有单帧，除非声明跨帧的外部资源；资源有对象池做缓冲
// shader的参数结构体里包括对RDG资源的声明，实际上是靠shader反射做的资源绑定？避免手动在cpp里再做一次绑定
// 状态设置等信息由一个parameters结构体描述，这个结构体的生命周期也应该与RDG一致（单帧），builder会给一个allocateParameters函数来返回

// 目前的RDG只实现了最基本的功能，相当多特性还未完成，例如：
// pass排序，pass剔除，多线程录制，multi queue，资源池GC，细粒度的资源处理（内存对齐，subresource屏障等），……
class RDGBuilder
{
public:
    RDGBuilder() = delete;
    RDGBuilder(RHICommandListRef command)
    : command(command)
    {}
    
    ~RDGBuilder() {};

    RDGTextureBuilder CreateTexture(std::string name);
    RDGBufferBuilder CreateBuffer(std::string name);
    RDGTextureBuilder GetOrCreateTexture(std::string name);
    RDGBufferBuilder GetOrCreateBuffer(std::string name);
    RDGRenderPassBuilder CreateRenderPass(std::string name);    // 假定所有pass的添加顺序就是执行顺序，方便处理排序和依赖关系等
    RDGComputePassBuilder CreateComputePass(std::string name);     
    RDGRayTracingPassBuilder CreateRayTracingPass(std::string name);
    RDGPresentPassBuilder CreatePresentPass(std::string name);    
    RDGCopyPassBuilder CreateCopyPass(std::string name);  

    RDGTextureHandle GetTexture(std::string name);
    RDGBufferHandle GetBuffer(std::string name);
    RDGRenderPassHandle GetRenderPass(std::string name)             { return GetPass<RDGRenderPassNodeRef, RDGRenderPassHandle>(name); }
    RDGComputePassHandle GetComputePass(std::string name)           { return GetPass<RDGComputePassNodeRef, RDGComputePassHandle>(name); }
    RDGRayTracingPassHandle GetRayTracingPass(std::string name)     { return GetPass<RDGRayTracingPassNodeRef, RDGRayTracingPassHandle>(name); }
    RDGPresentPassHandle GetPresentPass(std::string name)           { return GetPass<RDGPresentPassNodeRef, RDGPresentPassHandle>(name); }
    RDGCopyPassHandle GetCopyPass(std::string name)                 { return GetPass<RDGCopyPassNodeRef, RDGCopyPassHandle>(name); }

    RDGDependencyGraphRef GetGraph() { return graph; }

    void Execute();

private:
    void CreateInputBarriers(RDGPassNodeRef pass);
    void CreateOutputBarriers(RDGPassNodeRef pass);
    void PrepareDescriptorSet(RDGPassNodeRef pass);
    void PrepareRenderTarget(RDGRenderPassNodeRef pass, RHIRenderPassInfo& renderPassInfo);
    void ReleaseResource(RDGPassNodeRef pass);
    void ExecutePass(RDGRenderPassNodeRef pass);
    void ExecutePass(RDGComputePassNodeRef pass);
    void ExecutePass(RDGRayTracingPassNodeRef pass);
    void ExecutePass(RDGPresentPassNodeRef pass);
    void ExecutePass(RDGCopyPassNodeRef pass);

    template<typename Type, typename Handle>
    Handle GetPass(std::string name)
    {
        auto node = blackBoard.Pass(name);
        if(node == nullptr) 
        {
            LOG_DEBUG("Unable to find RDG resource, please check name!");
            return Handle(UINT32_MAX);
        }
        return dynamic_cast<Type>(node)->GetHandle();
    }

    RHITextureRef Resolve(RDGTextureNodeRef textureNode);   // 从node获取到实际的RHI资源
    RHIBufferRef Resolve(RDGBufferNodeRef bufferNode);     
    void Release(RDGTextureNodeRef textureNode, RHIResourceState state);  
    void Release(RDGBufferNodeRef bufferNode, RHIResourceState state);  

    RHIResourceState PreviousState(RDGTextureNodeRef textureNode, RDGPassNodeRef passNode, TextureSubresourceRange subresource = {}, bool output = false);    // 获取当前pass（在执行顺序上）的资源的前序状态
    RHIResourceState PreviousState(RDGBufferNodeRef bufferNode, RDGPassNodeRef passNode, uint32_t offset = 0, uint32_t size = 0, bool output = false);      // output用于标记是相对于输入还是输出资源
    bool IsLastUsedPass(RDGTextureNodeRef textureNode, RDGPassNodeRef passNode, bool output = false);
    bool IsLastUsedPass(RDGBufferNodeRef bufferNode, RDGPassNodeRef passNode, bool output = false);

    std::vector<RDGPassNodeRef> passes; // 创建的全部pass，按照创建顺序执行

    RDGDependencyGraphRef graph = std::make_shared<RDGDependencyGraph>();
    RDGBlackBoard blackBoard;

    RHICommandListRef command;
};
typedef std::shared_ptr<RDGBuilder> RDGBuilderRef;

class RDGTextureBuilder
{
public:
    RDGTextureBuilder(RDGBuilder* builder, RDGTextureNodeRef texture, bool validationMode = false) 
    : builder(builder)
    , texture(texture)
    , validationMode(validationMode) {};

    RDGTextureBuilder& Import(RHITextureRef texture, RHIResourceState initState); 
    RDGTextureBuilder& Exetent(Extent3D extent);
    RDGTextureBuilder& Format(RHIFormat format);
    RDGTextureBuilder& MemoryUsage(MemoryUsage memoryUsage);
    RDGTextureBuilder& AllowReadWrite();
    RDGTextureBuilder& AllowRenderTarget();
    RDGTextureBuilder& AllowDepthStencil();
    RDGTextureBuilder& MipLevels(uint32_t mipLevels);
    RDGTextureBuilder& ArrayLayers(uint32_t arrayLayers);

    RDGTextureHandle Finish() { return texture->GetHandle(); }

private:
    RDGBuilder* builder;
    RDGTextureNodeRef texture;
    bool validationMode = false;
};

class RDGBufferBuilder
{
public:
    RDGBufferBuilder(RDGBuilder* builder, RDGBufferNodeRef buffer, bool validationMode = false)
    : builder(builder)
    , buffer(buffer)
    , validationMode(validationMode) {};

    RDGBufferBuilder& Import(RHIBufferRef buffer, RHIResourceState initState);
    RDGBufferBuilder& Size(uint32_t size);
    RDGBufferBuilder& MemoryUsage(MemoryUsage memoryUsage);
    RDGBufferBuilder& AllowVertexBuffer();
    RDGBufferBuilder& AllowIndexBuffer();
    RDGBufferBuilder& AllowReadWrite();
    RDGBufferBuilder& AllowRead();

    RDGBufferHandle Finish() { return buffer->GetHandle(); }

private:
    RDGBuilder* builder;
    RDGBufferNodeRef buffer;
    bool validationMode = false;
};

class RDGRenderPassBuilder
{
public:
    RDGRenderPassBuilder(RDGBuilder* builder, RDGRenderPassNodeRef pass)
    : builder(builder)
    , pass(pass)
    , graph(builder->GetGraph()) {};

    RDGRenderPassBuilder& PassIndex(uint32_t x = 0, uint32_t y = 0, uint32_t z = 0);        // 给一个index设置函数方便给Execute传参
    RDGRenderPassBuilder& RootSignature(RHIRootSignatureRef rootSignature);                 // 若提供根签名未提供描述符，使用池化创建
    RDGRenderPassBuilder& DescriptorSet(uint32_t set, RHIDescriptorSetRef descriptorSet);   // 若提供了描述符，直接用相应的描述符
    RDGRenderPassBuilder& Sampler(uint32_t set, uint32_t binding, uint32_t index, RHISamplerRef sampler);
    RDGRenderPassBuilder& Read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGRenderPassBuilder& Read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});
    RDGRenderPassBuilder& ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);   // 好像和read也没什么区别？
    RDGRenderPassBuilder& ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {}); 
    RDGRenderPassBuilder& Color(uint32_t binding, RDGTextureHandle texture, 
                                AttachmentLoadOp load = ATTACHMENT_LOAD_OP_DONT_CARE, 
                                AttachmentStoreOp store = ATTACHMENT_STORE_OP_DONT_CARE, 
                                Color4 clearColor = {0.0f, 0.0f, 0.0f, 0.0f}, 
                                TextureSubresourceRange subresource = {}); 
    RDGRenderPassBuilder& DepthStencil( RDGTextureHandle texture, 
                                        AttachmentLoadOp load = ATTACHMENT_LOAD_OP_DONT_CARE, 
                                        AttachmentStoreOp store = ATTACHMENT_STORE_OP_DONT_CARE, 
                                        float clearDepth = 1.0f,
	                                    uint32_t clearStencil = 0,
                                        TextureSubresourceRange subresource = {});
    RDGRenderPassBuilder& Multiview(uint32_t multiviewCount);                                
    RDGRenderPassBuilder& OutputRead(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);             // 在执行完Pass后作为输出，自动屏障，可能还会在其他地方使用
    RDGRenderPassBuilder& OutputRead(RDGTextureHandle texture, TextureSubresourceRange subresource = {});      
    RDGRenderPassBuilder& OutputReadWrite(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGRenderPassBuilder& OutputReadWrite(RDGTextureHandle texture, TextureSubresourceRange subresource = {});  
    RDGRenderPassBuilder& Execute(const RDGPassExecuteFunc& execute);

    RDGRenderPassHandle Finish() { return pass->GetHandle(); }

private:
    RDGBuilder* builder;
    RDGRenderPassNodeRef pass;

    RDGDependencyGraphRef graph;
};

class RDGComputePassBuilder
{
public:
    RDGComputePassBuilder(RDGBuilder* builder, RDGComputePassNodeRef pass)
    : builder(builder)
    , pass(pass)
    , graph(builder->GetGraph()) {};

    RDGComputePassBuilder& PassIndex(uint32_t x = 0, uint32_t y = 0, uint32_t z = 0);        // 给一个index设置函数方便给Execute传参
    RDGComputePassBuilder& RootSignature(RHIRootSignatureRef rootSignature);                 // 若提供根签名未提供描述符，使用池化创建
    RDGComputePassBuilder& DescriptorSet(uint32_t set, RHIDescriptorSetRef descriptorSet);   // 若提供了描述符，直接用相应的描述符
    RDGComputePassBuilder& Sampler(uint32_t set, uint32_t binding, uint32_t index, RHISamplerRef sampler);
    RDGComputePassBuilder& Read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGComputePassBuilder& Read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {}); 
    RDGComputePassBuilder& ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);   // 好像和read也没什么区别？
    RDGComputePassBuilder& ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {}); 
    RDGComputePassBuilder& OutputRead(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);             // 在执行完Pass后作为输出，自动屏障，可能还会在其他地方使用
    RDGComputePassBuilder& OutputRead(RDGTextureHandle texture, TextureSubresourceRange subresource = {});      
    RDGComputePassBuilder& OutputReadWrite(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGComputePassBuilder& OutputReadWrite(RDGTextureHandle texture, TextureSubresourceRange subresource = {});  
    RDGComputePassBuilder& OutputIndirectDraw(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);


    RDGComputePassBuilder& Execute(const RDGPassExecuteFunc& execute);

    RDGComputePassHandle Finish() { return pass->GetHandle(); }

private:
    RDGBuilder* builder;
    RDGComputePassNodeRef pass;

    RDGDependencyGraphRef graph;
};

class RDGRayTracingPassBuilder
{
public:
    RDGRayTracingPassBuilder(RDGBuilder* builder, RDGRayTracingPassNodeRef pass)
    : builder(builder)
    , pass(pass)
    , graph(builder->GetGraph()) {};

    RDGRayTracingPassBuilder& PassIndex(uint32_t x = 0, uint32_t y = 0, uint32_t z = 0);        // 给一个index设置函数方便给Execute传参
    RDGRayTracingPassBuilder& RootSignature(RHIRootSignatureRef rootSignature);                 // 若提供根签名未提供描述符，使用池化创建
    RDGRayTracingPassBuilder& DescriptorSet(uint32_t set, RHIDescriptorSetRef descriptorSet);   // 若提供了描述符，直接用相应的描述符
    RDGRayTracingPassBuilder& Sampler(uint32_t set, uint32_t binding, uint32_t index, RHISamplerRef sampler);
    RDGRayTracingPassBuilder& Read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGRayTracingPassBuilder& Read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {}); 
    RDGRayTracingPassBuilder& ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);   // 好像和read也没什么区别？
    RDGRayTracingPassBuilder& ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {}); 
    RDGRayTracingPassBuilder& OutputRead(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);             // 在执行完Pass后作为输出，自动屏障，可能还会在其他地方使用
    RDGRayTracingPassBuilder& OutputRead(RDGTextureHandle texture, TextureSubresourceRange subresource = {});      
    RDGRayTracingPassBuilder& OutputReadWrite(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGRayTracingPassBuilder& OutputReadWrite(RDGTextureHandle texture, TextureSubresourceRange subresource = {});  


    RDGRayTracingPassBuilder& Execute(const RDGPassExecuteFunc& execute);

    RDGRayTracingPassHandle Finish() { return pass->GetHandle(); }

private:
    RDGBuilder* builder;
    RDGRayTracingPassNodeRef pass;

    RDGDependencyGraphRef graph;
};

class RDGPresentPassBuilder
{
public:
    RDGPresentPassBuilder(RDGBuilder* builder, RDGPresentPassNodeRef pass)
    : builder(builder)
    , pass(pass)
    , graph(builder->GetGraph()) {};

    RDGPresentPassHandle Finish() { return pass->GetHandle(); }

    RDGPresentPassBuilder& Texture(RDGTextureHandle texture, TextureSubresourceLayers subresource = {});
    RDGPresentPassBuilder& PresentTexture(RDGTextureHandle texture);

private:
    RDGBuilder* builder;
    RDGPresentPassNodeRef pass;

    RDGDependencyGraphRef graph;
};

class RDGCopyPassBuilder
{
public:
    RDGCopyPassBuilder(RDGBuilder* builder, RDGCopyPassNodeRef pass)
    : builder(builder)
    , pass(pass)
    , graph(builder->GetGraph()) {};
 
    RDGCopyPassHandle Finish() { return pass->GetHandle(); }

    RDGCopyPassBuilder& From(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGCopyPassBuilder& From(RDGTextureHandle texture, TextureSubresourceLayers subresource = {});
    RDGCopyPassBuilder& To(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGCopyPassBuilder& To(RDGTextureHandle texture, TextureSubresourceLayers subresource = {});
    
    RDGCopyPassBuilder& GenerateMips();
    RDGCopyPassBuilder& OutputRead(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGCopyPassBuilder& OutputRead(RDGTextureHandle texture, TextureSubresourceLayers subresource = {});
    RDGCopyPassBuilder& OutputReadWrite(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGCopyPassBuilder& OutputReadWrite(RDGTextureHandle texture, TextureSubresourceLayers subresource = {});

private:
    RDGBuilder* builder;
    RDGCopyPassNodeRef pass;

    RDGDependencyGraphRef graph;
};

