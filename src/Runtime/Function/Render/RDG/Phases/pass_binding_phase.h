#pragma once

#include "IRenderGraphPhase.h"
#include "pass_info_analysis.h"
#include "cross_queue_sync_analysis.h"

#include <unordered_map>
#include <utility>
#include <vector>

struct PassBindingConfig {
    bool enable_debug_output = false;
};

// 每个Pass的绑定结果（对应 SakuraEngine BindTablePhase 的 PassBindTableInfo）
struct PassBindInfo {
    // 描述符集（已分配+已更新，含显式传入的），set索引 → descriptor
    std::unordered_map<uint32_t, RHIDescriptorSetRef> descriptor_sets;
    // 池化 view（pass 结束后回收）
    std::vector<RHITextureViewRef> pooled_views;
    // 池化描述符集（全部 pass 执行完后回收）
    std::vector<std::pair<RHIDescriptorSetRef, uint32_t>> pooled_descriptor_sets;

    // render pass 的渲染信息（attachment view 在主线程预分配，随pooled_views统一归还）。
    // 并行录制期worker不碰视图池——RHIRenderingInfo必须整体前置到阶段6构建
    RHIRenderingInfo rendering_info = {};
};

// 资源生命期信息（对应 SakuraEngine ResourceLifetimeAnalysis 中释放所需的最小子集）
struct ResourceLifecycleInfo {
    struct LastUse {
        RDGPassNodeRef pass = nullptr;          // 最后一次使用的pass（按拓扑序）
        bool pass_has_output_edge = false;      // 该pass是否以output边写出该资源（同pass先读后写时，读不算最后使用）
    };

    std::unordered_map<RDGResourceNodeRef, LastUse> last_use;
};

// 阶段 6: Pass绑定阶段
//
// 合并了 SakuraEngine phases_v2 的三个阶段：
//   resource_allocation_phase —— Step1 集中式物理资源预分配（原 RDGBuilder::Resolve 的惰性分配改为帧首集中分配）
//   memory_aliasing_phase     —— 不单独移植。Tier0（纯池化）语义下就是"每资源独占分配"，
//                                本引擎 RDGPool 天然如此：状态随池条目跨帧传递，其 alias_transition
//                                的"池initState → 首用state"屏障语义由 node->initState + BarrierGenerationPhase 覆盖
//   bind_table_phase          —— Step3 集中式描述符集准备（原 RDGBuilder::PrepareDescriptorSet）
//
// Step2 的生命期分析为执行期释放（原 RDGBuilder::ReleaseResource/IsLastUsedPass）提供数据。
// 注意：描述符写入的是本阶段的 PassBindInfo map，不动 RDGPassNode 字段——
// 迁移期与旧路径 rdgBuilder->Execute() 并存互不干扰（旧路径见 descriptorSets 为空会自建一套）
//
class PassBindingPhase : public IRenderGraphPhase {
public:
    PassBindingPhase(
        const PassInfoAnalysis& pass_info_analysis,
        const CrossQueueSyncAnalysis& sync_analysis,
        const PassBindingConfig& config = {});
    ~PassBindingPhase() override = default;

    void reset_for_frame() override;
    void on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor) override;

    // ===== 下游查询接口 =====
    // 物理解析（对应 SakuraEngine ResourceAllocationPhase::get_resource）：
    // 阶段6保证非imported节点已分配；imported节点透传其引用
    RHITextureRef get_texture(RDGTextureNodeRef texture) const;
    RHIBufferRef  get_buffer(RDGBufferNodeRef buffer) const;

    // 绑定查询（对应 SakuraEngine BindTablePhase::get_pass_bind_table）
    const PassBindInfo* get_pass_bind_info(RDGPassNodeRef pass) const;

    // 生命期查询（供执行期释放）；output 为当前释放边的 IsOutput()，保留旧 IsLastUsedPass 的同pass先读后写规则
    bool is_last_use(RDGResourceNodeRef resource, RDGPassNodeRef pass, bool output) const;
    const ResourceLifecycleInfo& get_lifecycle_info() const { return lifecycle_; }

    const void debug_info()const;

private:
    // Step 1: 资源预分配（原 Resolve 的集中化版本）
    void allocate_resources(RDGDependencyGraphRef graph);

    // Step 2: 生命期分析（为 ReleaseResource 提供数据）
    void analyze_resource_lifetime(RDGDependencyGraphRef graph);

    // Step 3: 描述符集准备（原 PrepareDescriptorSet）
    void prepare_descriptor_sets(RDGDependencyGraphRef graph);

private:
    PassBindingConfig config_;

    const PassInfoAnalysis& pass_info_analysis_;
    const CrossQueueSyncAnalysis& sync_analysis_;   // 经 get_dependency_analysis() 提供Phase2拓扑序，并为将来按队列区分描述符池预留

    std::unordered_map<RDGPassNodeRef, PassBindInfo> pass_bind_infos_;
    ResourceLifecycleInfo lifecycle_;
};
