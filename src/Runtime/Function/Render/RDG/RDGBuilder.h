#pragma once
#include "Core/DependencyGraph/DependencyGraph.h"
#include "Core/Log/Log.h"
#include <initializer_list>
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RDG/RDGNode.h"
#include "Function/Render/RHI/RHICommandList.h"
#include "Function/Render/RHI/RHIStructs.h"

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

struct IRenderGraphPhase;


// UE中的RDG：
// 每个pass一个cpp文件 有graphbuilder的构建回调函数，
// meshpass继承pass，多一个获取场景meshbatch的回调函数
// RDG只有单帧生命周期，其分配的资源（返回引用句柄）也只有单帧，除非声明跨帧的外部资源；资源有对象池做缓冲
// shader的参数结构体里包括对RDG资源的声明，实际上是靠shader反射做的资源绑定？避免手动在cpp里再做一次绑定
// 状态设置等信息由一个parameters结构体描述，这个结构体的生命周期也应该与RDG一致（单帧），builder会给一个allocateParameters函数来返回

// 目前的RDG只实现了最基本的功能，相当多特性还未完成，例如：
// 资源池GC，细粒度的资源处理（内存对齐，subresource屏障等）
class RDGBuilder
{
public:
    RDGBuilder() = default;
    
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
    RDGTextureHandle FindTexture(const std::string& name);   // 静默查找：不存在返回无效句柄不告警（ReadIfValid配套，条件性资源的合法缺席）
    RDGBufferHandle GetBuffer(std::string name);
    RDGRenderPassHandle GetRenderPass(std::string name)             { return GetPass<RDGRenderPassNodeRef, RDGRenderPassHandle>(name); }
    RDGComputePassHandle GetComputePass(std::string name)           { return GetPass<RDGComputePassNodeRef, RDGComputePassHandle>(name); }
    RDGRayTracingPassHandle GetRayTracingPass(std::string name)     { return GetPass<RDGRayTracingPassNodeRef, RDGRayTracingPassHandle>(name); }
    RDGPresentPassHandle GetPresentPass(std::string name)           { return GetPass<RDGPresentPassNodeRef, RDGPresentPassHandle>(name); }
    RDGCopyPassHandle GetCopyPass(std::string name)                 { return GetPass<RDGCopyPassNodeRef, RDGCopyPassHandle>(name); }

    RDGDependencyGraphRef GetGraph() { return graph; }

    friend struct IRenderGraphPhase;

private:


    template<typename Type, typename Handle>
    Handle GetPass(std::string name)
    {
        auto node = blackBoard.Pass(name);
        if(node == nullptr) 
        {
            LOG_DEBUG("Unable to find RDG resource, please check name!");
            return Handle(UINT64_MAX);
        }
        return dynamic_cast<Type>(node)->GetHandle();
    }

    RDGDependencyGraphRef graph = std::make_shared<RDGDependencyGraph>();
    RDGBlackBoard blackBoard;
};
using RDGBuilderRef = std::shared_ptr<RDGBuilder>;

class RDGTextureBuilder
{
public:
    RDGTextureBuilder(RDGBuilder* builder, RDGTextureNodeRef texture, bool validationMode = false) 
    : builder(builder)
    , texture(texture)
    , validationMode(validationMode) {};

    // 布局动态记载制（2026-10-26）：Import不再声明稳态布局——Phase 7种子优先取registry记录
    //（上帧末真实布局，逐子资源），initState仅作【首见兜底】（registry未命中=进程首见）。
    // 默认UNDEFINED（RDG出生纹理的真实初态）；RDG外出生带内容的资源用bornState重载
    //（文件加载/预烘焙，如BRDF LUT）。旧"稳态声明契约"退役（调用点的末位触碰者考古注释作废）
    RDGTextureBuilder& Import(RHITextureRef texture);                                            // 首见兜底UNDEFINED
    RDGTextureBuilder& Import(RHITextureRef texture, RHIResourceState bornState);                // 首见兜底bornState（外部出生特案）
    RDGTextureBuilder& Exetent(Extent3D extent);
    RDGTextureBuilder& Format(RHIFormat format);
    RDGTextureBuilder& MemoryUsage(MemoryUsage memoryUsage);
    RDGTextureBuilder& AllowReadWrite();
    RDGTextureBuilder& AllowConcurrent(std::initializer_list<QueueType> families);
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

    // buffer侧暂保留声明制（纹理侧已改布局动态记载，见上）：buffer无布局维度，声明撒谎风险
    //（如"Cluster Draw Infos"类稳态注释）可用同配方迁移——registry记录加单state字段
    RDGBufferBuilder& Import(RHIBufferRef buffer, RHIResourceState initState, bool firstFrameUndefined = true);
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
    RDGRenderPassBuilder& Shaders(std::initializer_list<RHIShaderRef> shaders);
    RDGRenderPassBuilder& Conditional(uint32_t set, uint32_t binding);                             // 刀7b-5：手动条件槽留痕（动态分支守护的静态访问，如view_z.comp的SETTING.side）              // 刀7b-5：声明本子pass的shader（严格边校验；共享RS多子pass形态必须声明，否则走RS聚合弱语义）
    RDGRenderPassBuilder& DescriptorSet(uint32_t set, RHIDescriptorSetRef descriptorSet);   // 若提供了描述符，直接用相应的描述符
    RDGRenderPassBuilder& Read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0, ResourceType type = RESOURCE_TYPE_UNIFORM_BUFFER);
    RDGRenderPassBuilder& Read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});
    RDGRenderPassBuilder& ReadIfValid(uint32_t set, uint32_t binding, uint32_t index, const std::string& name, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});  // 条件性资源：名字缺席则静默跳过
    RDGRenderPassBuilder& ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);   // 好像和read也没什么区别？
    RDGRenderPassBuilder& ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});
    // S3:反射驱动描述符边——type/frequency由.Shaders()反射条目回填并校验(旧slotted Read/ReadWrite族仍在,S5b删)
    RDGRenderPassBuilder& ReadTexture(uint32_t set, uint32_t binding, RDGTextureHandle texture, uint32_t index = 0, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});
    RDGRenderPassBuilder& ReadWriteTexture(uint32_t set, uint32_t binding, RDGTextureHandle texture, uint32_t index = 0, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});
    RDGRenderPassBuilder& ReadBuffer(uint32_t set, uint32_t binding, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0, uint32_t index = 0);
    RDGRenderPassBuilder& ReadWriteBuffer(uint32_t set, uint32_t binding, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0, uint32_t index = 0);
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
    
    RDGRenderPassBuilder& Read(RDGBufferHandle buffer);                        // 图外buffer读取（shader读类别）
    RDGRenderPassBuilder& Read(RDGTextureHandle texture);                      // 图外纹理采样（per-frame set等）
    RDGRenderPassBuilder& ReadIndirect(RDGBufferHandle buffer);                // 间接命令buffer（DrawIndirect固定功能抓取，与GPU Culling的OutputIndirectDraw产出状态对齐）
    RDGRenderPassBuilder& ReadAccelerationStructure(RDGBufferHandle buffer);   // ray query读AS
    RDGRenderPassBuilder& OutputReadWrite(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGRenderPassBuilder& OutputReadWrite(RDGTextureHandle texture, TextureSubresourceRange subresource = {});  
    RDGRenderPassBuilder& Execute(const RDGPassExecuteFunc& execute);
    RDGRenderPassBuilder& AddFlag(const RDGPassFlags flag);

    RDGRenderPassHandle Finish()
    {
        // 构建期完备性检查:set2反射入口必须全部被描述符边补齐
        // (S3-S5过渡:仅对新边API已启用的pass生效,防止存量旧式边全库误杀;S5b拆门)
        if (pass->bindingApiMigrated)
            for (size_t i = 0; i < pass->set2Entries.size(); i++)
                if (!pass->set2EntryDone[i])
                    LOG_FATAL("pass[%s]: set2入口未补齐 binding=%u size=%u type=0x%x frequency=0x%x(着色器静态使用该槽位但无边)", pass->Name().c_str(), pass->set2Entries[i].binding, pass->set2Entries[i].size, pass->set2Entries[i].type, pass->set2Entries[i].frequency);
        return pass->GetHandle();
    }

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
    RDGComputePassBuilder& Shaders(std::initializer_list<RHIShaderRef> shaders);
    RDGComputePassBuilder& Conditional(uint32_t set, uint32_t binding);                             // 刀7b-5：手动条件槽留痕（动态分支守护的静态访问，如view_z.comp的SETTING.side）              // 刀7b-5：同上（严格边校验声明）
    RDGComputePassBuilder& DescriptorSet(uint32_t set, RHIDescriptorSetRef descriptorSet);   // 若提供了描述符，直接用相应的描述符
    RDGComputePassBuilder& Read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0, ResourceType type = RESOURCE_TYPE_UNIFORM_BUFFER);
    RDGComputePassBuilder& Read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {}); 
    RDGComputePassBuilder& ReadIfValid(uint32_t set, uint32_t binding, uint32_t index, const std::string& name, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});   // 条件性资源：名字缺席则静默跳过
    RDGComputePassBuilder& ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);   // 好像和read也没什么区别？
    RDGComputePassBuilder& ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});
    // S3:反射驱动描述符边——type/frequency由.Shaders()反射条目回填并校验(旧slotted Read/ReadWrite族仍在,S5b删)
    RDGComputePassBuilder& ReadTexture(uint32_t set, uint32_t binding, RDGTextureHandle texture, uint32_t index = 0, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});
    RDGComputePassBuilder& ReadWriteTexture(uint32_t set, uint32_t binding, RDGTextureHandle texture, uint32_t index = 0, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});
    RDGComputePassBuilder& ReadBuffer(uint32_t set, uint32_t binding, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0, uint32_t index = 0);
    RDGComputePassBuilder& ReadWriteBuffer(uint32_t set, uint32_t binding, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0, uint32_t index = 0);
    RDGComputePassBuilder& OutputRead(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);             // 在执行完Pass后作为输出，自动屏障，可能还会在其他地方使用
    RDGComputePassBuilder& OutputRead(RDGTextureHandle texture, TextureSubresourceRange subresource = {});      
    RDGComputePassBuilder& OutputReadWrite(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGComputePassBuilder& OutputReadWrite(RDGTextureHandle texture, TextureSubresourceRange subresource = {});  
    RDGComputePassBuilder& OutputIndirectDraw(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGComputePassBuilder& AddFlag(const RDGPassFlags flag);

    RDGComputePassBuilder& Read(RDGBufferHandle buffer);
    RDGComputePassBuilder& Read(RDGTextureHandle texture);
    RDGComputePassBuilder& ReadIndirect(RDGBufferHandle buffer);
    RDGComputePassBuilder& ReadAccelerationStructure(RDGBufferHandle buffer);

    RDGComputePassBuilder& Execute(const RDGPassExecuteFunc& execute);

    RDGComputePassHandle Finish()
    {
        // 构建期完备性检查:set2反射入口必须全部被描述符边补齐
        // (S3-S5过渡:仅对新边API已启用的pass生效,防止存量旧式边全库误杀;S5b拆门)
        if (pass->bindingApiMigrated)
            for (size_t i = 0; i < pass->set2Entries.size(); i++)
                if (!pass->set2EntryDone[i])
                    LOG_FATAL("pass[%s]: set2入口未补齐 binding=%u size=%u type=0x%x frequency=0x%x(着色器静态使用该槽位但无边)", pass->Name().c_str(), pass->set2Entries[i].binding, pass->set2Entries[i].size, pass->set2Entries[i].type, pass->set2Entries[i].frequency);
        return pass->GetHandle();
    }

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
    RDGRayTracingPassBuilder& Shaders(std::initializer_list<RHIShaderRef> shaders);
    RDGRayTracingPassBuilder& Conditional(uint32_t set, uint32_t binding);                             // 刀7b-5：手动条件槽留痕（动态分支守护的静态访问，如view_z.comp的SETTING.side）              // 刀7b-5：同上（严格边校验声明）
    RDGRayTracingPassBuilder& DescriptorSet(uint32_t set, RHIDescriptorSetRef descriptorSet);   // 若提供了描述符，直接用相应的描述符
    RDGRayTracingPassBuilder& Read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGRayTracingPassBuilder& Read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {}); 
    RDGRayTracingPassBuilder& ReadIfValid(uint32_t set, uint32_t binding, uint32_t index, const std::string& name, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});  // 条件性资源：名字缺席则静默跳过
    RDGRayTracingPassBuilder& ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);   // 好像和read也没什么区别？
    RDGRayTracingPassBuilder& ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});
    // S3:反射驱动描述符边——type/frequency由.Shaders()反射条目回填并校验(旧slotted Read/ReadWrite族仍在,S5b删)
    RDGRayTracingPassBuilder& ReadTexture(uint32_t set, uint32_t binding, RDGTextureHandle texture, uint32_t index = 0, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});
    RDGRayTracingPassBuilder& ReadWriteTexture(uint32_t set, uint32_t binding, RDGTextureHandle texture, uint32_t index = 0, TextureViewType viewType = VIEW_TYPE_2D, TextureSubresourceRange subresource = {});
    RDGRayTracingPassBuilder& ReadBuffer(uint32_t set, uint32_t binding, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0, uint32_t index = 0);
    RDGRayTracingPassBuilder& ReadWriteBuffer(uint32_t set, uint32_t binding, RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0, uint32_t index = 0);
    RDGRayTracingPassBuilder& OutputRead(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);             // 在执行完Pass后作为输出，自动屏障，可能还会在其他地方使用
    RDGRayTracingPassBuilder& OutputRead(RDGTextureHandle texture, TextureSubresourceRange subresource = {});
    RDGRayTracingPassBuilder& OutputReadWrite(RDGBufferHandle buffer, uint32_t offset = 0, uint32_t size = 0);
    RDGRayTracingPassBuilder& OutputReadWrite(RDGTextureHandle texture, TextureSubresourceRange subresource = {});

    RDGRayTracingPassBuilder& Read(RDGBufferHandle buffer);
    RDGRayTracingPassBuilder& Read(RDGTextureHandle texture);                  // 图外纹理采样（如阴影图）
    RDGRayTracingPassBuilder& ReadIndirect(RDGBufferHandle buffer);
    RDGRayTracingPassBuilder& ReadAccelerationStructure(RDGBufferHandle buffer);
    RDGRayTracingPassBuilder& AddFlag(const RDGPassFlags flag);

    RDGRayTracingPassBuilder& Execute(const RDGPassExecuteFunc& execute);

    RDGRayTracingPassHandle Finish()
    {
        // 构建期完备性检查:set2反射入口必须全部被描述符边补齐
        // (S3-S5过渡:仅对新边API已启用的pass生效,防止存量旧式边全库误杀;S5b拆门)
        if (pass->bindingApiMigrated)
            for (size_t i = 0; i < pass->set2Entries.size(); i++)
                if (!pass->set2EntryDone[i])
                    LOG_FATAL("pass[%s]: set2入口未补齐 binding=%u size=%u type=0x%x frequency=0x%x(着色器静态使用该槽位但无边)", pass->Name().c_str(), pass->set2Entries[i].binding, pass->set2Entries[i].size, pass->set2Entries[i].type, pass->set2Entries[i].frequency);
        return pass->GetHandle();
    }

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
    RDGCopyPassBuilder& AddFlag(const RDGPassFlags flag);

private:
    RDGBuilder* builder;
    RDGCopyPassNodeRef pass;

    RDGDependencyGraphRef graph;
};

