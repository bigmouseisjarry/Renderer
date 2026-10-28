#pragma once

#include "Function/Global/Definations.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "MurmurHash2.h"

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <unordered_map>

// RDG所用到的主要的资源，由于每帧重构，都需要池化
// 包括buffer texture textureView等
// 录制每个pass的命令时会申请此处的实际RHI资源，录制完成后再将资源归还给池子

// TODO 目前并没有做池化后的GC，冗余资源没有定期删除

// 三个资源池按帧槽分池的原因（多队列改造，c981e32a）：
// 池条目在录制结束后立即归还（release_sweep），此时帧N的GPU工作仍在飞——旧单队列下依赖"单队列提交按FIFO串行执行"保证帧N/N+1两次使用永不并发，故可全局共享；
// 多队列下帧间无任何排序（帧内timeline链不跨帧），共享池会把帧N还在用的资源发给帧N+1造成真并发竞态（纹理还叠加layout/QFO冲突）。
// 按槽分池后资源只在帧N与帧N+2间复用，而同槽复用被RenderSystem::Tick的帧栅栏挡住——安全保证从"队列FIFO"换成"帧栅栏"。
// TextureView池同理的原因：view绑定创建时的具体纹理实例，共享会绕过纹理池的槽隔离；
// 描述符池则自初始提交就分槽（录制期被CPU写入，host写连单队列FIFO都救不了）。

class RDGBufferPool
{
public:
    struct PooledBuffer
    {
        RHIBufferRef buffer;    // RHIBuffer的析构是在RHI里追踪完成的，无须手动释放
        RHIResourceState state; // 当前所处的资源状态
    };

    struct Key
    {
        Key(const RHIBufferInfo& info) 
        : memoryUsage(info.memoryUsage)
        , type(info.type)
        , creationFlag(info.creationFlag)
        {}
  
        // uint64_t size;  // size不作为键，只要大小够就行

        MemoryUsage memoryUsage;
        ResourceType type;

        BufferCreationFlags creationFlag;

        friend bool operator== (const Key& a, const Key& b)
        {
            return  a.memoryUsage == b.memoryUsage &&
                    a.type == b.type &&
                    a.creationFlag == b.creationFlag;
        }

        struct Hash {
            size_t operator()(const Key& a) const {
                return MurmurHash64A(&a, sizeof(Key), 0);        
            }
        };
    };

    PooledBuffer Allocate(const RHIBufferInfo& info);
    void Release(const PooledBuffer& pooledBuffer);

    inline uint32_t PooledSize()    { return pooledSize; }
    inline uint32_t AllocatedSize() { return allocatedSize; }
    void Clear()                    { pooledBuffers.clear(); pooledSize = 0; }

    static std::shared_ptr<RDGBufferPool> Get(uint32_t frameIndex)
    {
        static std::shared_ptr<RDGBufferPool> pool[FRAMES_IN_FLIGHT];
        if(pool[frameIndex] == nullptr) pool[frameIndex] = std::make_shared<RDGBufferPool>();
        return pool[frameIndex];
    }

private:
    std::unordered_map<Key, std::list<PooledBuffer>, Key::Hash> pooledBuffers;    // 可能有多个满足需求的buffer，还得再靠size等筛选
    uint32_t pooledSize = 0;
    uint32_t allocatedSize = 0;
};


class RDGTexturePool
{
public:
    struct PooledTexture
    {
        RHITextureRef texture;
        RHIResourceState state;
        uint32_t queueFamily = RHI_QUEUE_FAMILY_IGNORED;    // 归还时的归属族（跨帧所有权转移：下一帧异族首用发acquire）
    };

    struct Key
    {
        Key(const RHITextureInfo& info) 
        : info(info)
        {}

        RHITextureInfo info;

        friend bool operator== (const Key& a, const Key& b)
        {
            return  a.info == b.info;
        }

        struct Hash {
            size_t operator()(const Key& a) const {
                return MurmurHash64A(&a, sizeof(Key), 0);  
            }
        };
    };

    PooledTexture Allocate(const RHITextureInfo& info);
    void Release(const PooledTexture& pooledTexture);

    inline uint32_t PooledSize()    { return pooledSize; }
    inline uint32_t AllocatedSize() { return allocatedSize; }
    void Clear()                    { pooledTextures.clear(); pooledSize = 0; }

    static std::shared_ptr<RDGTexturePool> Get(uint32_t frameIndex)
    {
        static std::shared_ptr<RDGTexturePool> pool[FRAMES_IN_FLIGHT];
        if(pool[frameIndex] == nullptr) pool[frameIndex] = std::make_shared<RDGTexturePool>();
        return pool[frameIndex];
    }

private:
    std::unordered_map<Key, std::list<PooledTexture>, Key::Hash> pooledTextures;  
    uint32_t pooledSize = 0;
    uint32_t allocatedSize = 0;
};


class RDGTextureViewPool
{
public:
    struct PooledTextureView
    {
        RHITextureViewRef textureView;  
    };

    struct Key
    {
        Key(const RHITextureViewInfo& info) 
        : info(info)
        {}

        RHITextureViewInfo info;	

        friend bool operator== (const Key& a, const Key& b)
        {
            return  a.info == b.info;
        }

        struct Hash {
            size_t operator()(const Key& a) const {
                return MurmurHash64A(&a.info, sizeof(RHITextureViewInfo), 0);
            }
        };
    };

    PooledTextureView Allocate(const RHITextureViewInfo& info);
    void Release(const PooledTextureView& pooledTextureView);

    inline uint32_t PooledSize()    { return pooledSize; }
    inline uint32_t AllocatedSize() { return allocatedSize; }
    void Clear()                    { pooledTextureViews.clear(); pooledSize = 0; }

    static std::shared_ptr<RDGTextureViewPool> Get(uint32_t frameIndex)
    {
        static std::shared_ptr<RDGTextureViewPool> pool[FRAMES_IN_FLIGHT];
        if(pool[frameIndex] == nullptr) pool[frameIndex] = std::make_shared<RDGTextureViewPool>();
        return pool[frameIndex];
    }

private:
    std::unordered_map<Key, std::list<PooledTextureView>, Key::Hash> pooledTextureViews;
    uint32_t pooledSize = 0;
    uint32_t allocatedSize = 0;
};

class RDGDescriptorSetPool
{
public:
    struct PooledDescriptor
    {
        RHIDescriptorSetRef descriptor;  
    };

    // 池键=本pass set2反射used条目足迹(频率归一ALL)+set号;同键⟺实际使用槽位集合相同
    // ⟹跨足迹陈旧槽污染链断绝(旧键=整个RS entries+set,全引擎pass共用同一套全局拼装RS→单桶,
    // 前任pass的陈旧槽描述符残留→VUID-vkCmdDraw-None-09600,治本方案)
    // RS仅用于桶miss时CreateDescriptorSet与静态采样器重播,布局真值不变(键只路由分桶)
    struct Key
    {
        // 唯一构造点MakeKey:条目按值拷贝后归一(frequency→ALL,used→true),
        // 并用值初始化条目逐字段拷贝以清零padding——保证同足迹键的Murmur字节哈希跨pass确定一致
        static Key MakeKey(const std::vector<ShaderResourceEntry>& set2Entries, uint32_t set)
        {
            Key k;
            k.set = set;
            k.entries.reserve(set2Entries.size());
            for (const ShaderResourceEntry& e : set2Entries)
            {
                ShaderResourceEntry n{};    // 值初始化:padding归零
                n.set = e.set; n.binding = e.binding; n.size = e.size;
                n.frequency = SHADER_FREQUENCY_ALL;    // 频率不进键:描述符内容不区分频率,归一后同足迹图形/计算pass共桶
                n.type = e.type; n.used = true;        // used全真(条目数组本就只含used,防御归一;Murmur字节哈希覆盖used而==不比,归一后两语义一致)
                k.entries.push_back(n);
            }
            return k;
        }

        // set0/1专用:沿用改键前的RS-entries语义(字节不变)——
        // ★set1巨型bindless集必须单桶:NRD静态采样器领用set1,若按足迹分桶,
        // NRD的3个子pass足迹各异→3套巨型set1×帧槽→显存乘法(OOM定案同型,R5卫生)
        static Key MakeLegacyKey(const RHIRootSignatureInfo& info, uint32_t set)
        {
            Key k;
            k.entries = info.GetEntries();
            k.set = set;
            return k;
        }

        std::vector<ShaderResourceEntry> entries;
        uint32_t set = 0;

        friend bool operator== (const Key& a, const Key& b)
        {
            return  a.entries == b.entries&&            // used不参与==:键经MakeKey归一后used恒真,哈希与==语义一致
                    a.set == b.set;
        }

        struct HashEntries {
            size_t operator()(std::vector<ShaderResourceEntry> entries) const {
                return MurmurHash64A(entries.data(), entries.size() * sizeof(ShaderResourceEntry), 0);  
            }
        };

        struct Hash {
            size_t operator()(const Key& a) const {
                return  std::hash<uint32_t>()(a.set) ^
                        (HashEntries()(a.entries) << 1);
            }
        };
    };

    PooledDescriptor Allocate(const Key& key, const RHIRootSignatureRef& rootSignature);   // 键只路由分桶;RS用于桶miss时CreateDescriptorSet+采样器重播
    void Release(const Key& key, const PooledDescriptor& pooledDescriptor);

    inline uint32_t PooledSize()    { return pooledSize; }
    inline uint32_t AllocatedSize() { return allocatedSize; }
    void Clear()                    { pooledDescriptors.clear(); pooledSize = 0; }

    static std::shared_ptr<RDGDescriptorSetPool> Get(uint32_t index)    // 描述符池需要FRAMES_IN_FLIGHT每帧一个，不然下一帧修改可能影响上一帧还未完成的渲染！！！
    {                                                                   
        static std::shared_ptr<RDGDescriptorSetPool> pool[FRAMES_IN_FLIGHT];
        if(pool[index] == nullptr) pool[index] = std::make_shared<RDGDescriptorSetPool>();
        return pool[index];
    }

private:
    std::unordered_map<Key, std::list<PooledDescriptor>, Key::Hash> pooledDescriptors;
    uint32_t pooledSize = 0;
    uint32_t allocatedSize = 0;
};
