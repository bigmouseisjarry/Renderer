#pragma once
#include "Function/Render/RDG/RDGNode.h"
#include "Function/Render/RDG/Phases/IRenderGraphPhase.h"
// 资源访问类型
enum class EResourceAccessType : uint32_t
{
    Read,
    Write,
    ReadWrite
};

// 单条资源访问信息
struct ResourceAccessInfo {
    RDGPassNodeRef pass = nullptr;
    RDGResourceNodeRef resource = nullptr;
    EResourceAccessType access_type;
    RHIResourceState resource_state = RESOURCE_STATE_UNDEFINED;
    uint32_t frequency = SHADER_FREQUENCY_ALL;    // 反射descriptor边=该槽位shader使用频率（屏障阶段收窄用）；非反射边（Color/DS/Copy/虚拟）保持ALL
    
    // Texture subresource range (optional)
    // 以下四值经PassInfoAnalysis展开站具体化（本结构从边提取时aspect=NONE→format推导、
    // count=0→节点全量已写回边），下游消费一律直读真值、不再展开
    TextureAspectFlags aspect = TEXTURE_ASPECT_NONE;
    uint32_t mip_base = 0;
    uint32_t mip_count = 0;
    uint32_t array_base = 0;
    uint32_t array_count = 0;
    
    // Buffer range (optional)
    uint64_t buffer_from = 0;
    uint64_t buffer_to = 0;
};

// pass的所有资源访问信息
struct PassResourceInfo {
    std::vector<ResourceAccessInfo> resource_accesses; 
    uint32_t total_resource_count = 0;
};

// pass标志
struct PassPerformanceInfo {
    bool is_compute_intensive = false;    // ComputeIntensive flag
    bool is_bandwidth_intensive = false;  // BandwidthIntensive flag
    bool is_vertex_bound = false;         // VertexBoundIntensive flag
    bool is_pixel_bound = false;          // PixelBoundIntensive flag
    bool has_small_working_set = false;   // SmallWorkingSet flag
    bool has_large_working_set = false;   // LargeWorkingSet flag
    bool has_random_access = false;       // RandomAccess flag
    bool has_streaming_access = false;    // StreamingAccess flag
    bool prefers_async_compute = false;   // PreferAsyncCompute flag
    bool separate_command_buffer = false; // SeperateFromCommandBuffer flag
};

// pass信息
struct PassInfo {
    RDGPassNodeRef pass = nullptr;
    RDGPassNodeType pass_type = RDGPassNodeType::RDG_PASS_NODE_TYPE_RENDER;
    PassResourceInfo resource_info;
    PassPerformanceInfo performance_info;
};

// 资源信息
struct ResourceInfo {
    RDGResourceNodeRef resource = nullptr;
    uint64_t memory_size = 0;
    uint8_t access_queues = 0;  // 位掩码: bit0=Graphics, bit1=Compute, bit2=Copy

    void add_queue(QueueType q) { access_queues |= (1u << static_cast<uint8_t>(q)); }
    bool has_queue(QueueType q) const { return (access_queues & (1u << static_cast<uint8_t>(q))) != 0; }

    std::unordered_map<RDGPassNodeRef, RHIResourceState> used_states;
};


// Analysis phase - runs before DependencyAnalysis
class PassInfoAnalysis : public IRenderGraphPhase
{
public:
    PassInfoAnalysis() = default;
    ~PassInfoAnalysis() override = default;

    void reset_for_frame() override;

    void on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor) override;

    // 查询接口
    const PassInfo* get_pass_info(RDGPassNodeRef pass) const;
    const ResourceInfo* get_resource_info(RDGResourceNodeRef resource) const;
    const PassResourceInfo* get_resource_info(RDGPassNodeRef pass) const;
    const PassPerformanceInfo* get_performance_info(RDGPassNodeRef pass) const;

    // 取到这个pass对这个资源的访问类型
    EResourceAccessType get_resource_access_type(RDGPassNodeRef pass, RDGResourceNodeRef resource) const;
    // 取到这个资源在这个pass的状态
    RHIResourceState get_resource_state(RDGPassNodeRef pass, RDGResourceNodeRef resource) const;

    // 资源状态重路由查询接口
    bool resource_needs_rerouting(RDGResourceNodeRef resource) const;

private:
    void extract_pass_info(RDGPassNodeRef pass);
    void extract_resource_info(RDGPassNodeRef pass, PassResourceInfo& info);
    void extract_performance_info(RDGPassNodeRef pass, PassPerformanceInfo& info);

private:
    std::unordered_map<RDGPassNodeRef, PassInfo> pass_infos;
    std::unordered_map<RDGResourceNodeRef, ResourceInfo> resource_infos; // For dependency analysis
};
