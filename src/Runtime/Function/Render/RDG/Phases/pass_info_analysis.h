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

// Resource access info - detailed for dependency analysis
struct ResourceAccessInfo {
    RDGPassNodeRef pass = nullptr;
    RDGResourceNodeRef resource = nullptr;
    EResourceAccessType access_type;
    RHIResourceState resource_state = RESOURCE_STATE_UNDEFINED;
    bool is_output = false;    // 是否为output边（pass产出、后续pass消费）：其状态是pass之后的收敛状态，屏障需在pass之后发射
    // Texture subresource range (optional)
    uint32_t mip_base = 0;
    uint32_t mip_count = 0;
    uint32_t array_base = 0;
    uint32_t array_count = 0;
    // Buffer range (optional)
    uint64_t buffer_from = 0;
    uint64_t buffer_to = 0;
};

// Resource info - direct extraction with detailed access info
struct PassResourceInfo {
    std::vector<ResourceAccessInfo> resource_accesses; // For dependency analysis
    uint32_t total_resource_count = 0;
};

// Performance info - from hints only
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

// Pass info container
struct PassInfo {
    RDGPassNodeRef pass = nullptr;
    RDGPassNodeType pass_type = RDGPassNodeType::RDG_PASS_NODE_TYPE_RENDER;
    PassResourceInfo resource_info;
    PassPerformanceInfo performance_info;
};

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

    // IRenderGraphPhase interface
    void on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor) override;

    // 查询接口
    const PassInfo* get_pass_info(RDGPassNodeRef pass) const;
    const ResourceInfo* get_resource_info(RDGResourceNodeRef resource) const;
    const PassResourceInfo* get_resource_info(RDGPassNodeRef pass) const;
    const PassPerformanceInfo* get_performance_info(RDGPassNodeRef pass) const;

    // For dependency analysis - avoid recomputation
    EResourceAccessType get_resource_access_type(RDGPassNodeRef pass, RDGResourceNodeRef resource) const;
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
