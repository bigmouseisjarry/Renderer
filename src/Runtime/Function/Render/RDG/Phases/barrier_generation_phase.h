#pragma once

#include "IRenderGraphPhase.h"
#include "pass_info_analysis.h"
#include "pass_binding_phase.h"
#include "cross_queue_sync_analysis.h"
#include "schedule_reorder.h"

#include <unordered_map>
#include <vector>

// 屏障类型
enum class EBarrierType : uint8_t
{
    ResourceTransition = 0,     // 状态转移屏障（含跨队列转移）
    CrossQueueSync,             // 预留：跨队列同步屏障（多队列提交期由信号量/时间线实现）
    MemoryAliasing,             // 预留：内存别名屏障（Tier0池化语义下由 initState 转移覆盖）
};

// RDG屏障——虚拟屏障：持有资源节点指针，发射时由执行期解析物理对象
// （沿用 SakuraEngine 的设计：屏障生成不依赖物理资源，即使阶段6已完成分配）
struct RDGBarrier
{
    RDGResourceNodeRef resource = nullptr;
    EBarrierType type = EBarrierType::ResourceTransition;

    RDGPassNodeRef source_pass = nullptr;   // 上一次触碰该资源的pass
    RDGPassNodeRef target_pass = nullptr;   // 本次访问的pass（屏障挂在该pass的batch）
    uint32_t source_queue = 0;              // 队列索引（多队列提交期使用）
    uint32_t target_queue = 0;

    RHIResourceState before_state = RESOURCE_STATE_UNDEFINED;
    RHIResourceState after_state = RESOURCE_STATE_UNDEFINED;

    // 发射位置：非output边（Read/ReadWrite/Color/DepthStencil/Transfer等）的状态是pass期间状态，
    // 屏障在pass之前发射；output边（OutputRead/OutputReadWrite等）的状态是pass之后的收敛状态，
    // 屏障在pass之后发射——对应旧路径 CreateInputBarriers / CreateOutputBarriers 的时序
    bool after_pass = false;

    bool is_subresource = false;            // 是否为子范围访问（非整个资源）
    // 纹理子资源范围（已解析为具体数值，0不做默认解释）
    TextureSubresourceRange subresource = {};
    // buffer范围
    uint32_t buffer_offset = 0;
    uint32_t buffer_size = 0;
};

// 一个pass的屏障批次（按类型分组，对应 SakuraEngine 的 BarrierBatch）
struct BarrierBatch
{
    std::vector<RDGBarrier> barriers;
    EBarrierType batch_type = EBarrierType::ResourceTransition;
};

struct BarrierGenerationResult
{
    // pass → 该pass发射前需要插入的屏障批次
    std::unordered_map<RDGPassNodeRef, std::vector<BarrierBatch>> pass_barrier_batches;

    // 统计
    uint32_t total_barriers = 0;
};

struct BarrierGenerationConfig
{
    bool enable_debug_output = false;
    // split barrier（begin/end分离）暂不移植：Sakura的实现有end误加入begin batch的bug，且需要硬件成本估算支持
    // bool enable_split_barriers = false;
};

// 阶段 7: 屏障生成阶段
//
// 从 SakuraEngine barrier_generation_phase 移植，两处简化：
//   1. 不做 split barrier（见config注释）
//   2. 不生成独立的 cross-queue-sync / memory-aliasing 屏障：
//      转移屏障的"队列不同"判定已覆盖所有跨队列状态变化（与SSIS同步点同源），
//      optimized_sync_points 留给执行期做多队列信号量插入；池化 initState 转移已覆盖aliasing语义
//
// 替代旧路径的 CreateInputBarriers/CreateOutputBarriers/PreviousState 内联扫描：
// 每资源一个 subresource 级状态跟踪器（纹理按 mipLevels*arrayLayers 展开），
// 初始状态 = imported 声明的 initState / created 资源的池化跨帧状态（阶段6分配结果），
// 按 Phase 2 拓扑序遍历每个pass的 resource_accesses 生成转移屏障：
//   先处理非output访问（屏障在pass之前发射），再处理output访问（屏障在pass之后发射），
//   与旧路径 输入屏障 → pass → 输出屏障 的时序一致
// should_barrier 规则：队列不同 / 状态变化 / UAV→UAV（同状态也需要写后读可见性）
//
// Present/Copy 的pass内动态屏障（swapchain PRESENT循环、generateMip的mip链屏障）
// 仍由执行期手工发射——它们发生在pass命令之间，访问记录粒度表达不了
//
class BarrierGenerationPhase : public IRenderGraphPhase
{
public:
    BarrierGenerationPhase(
        const CrossQueueSyncAnalysis& sync_analysis,
        const PassBindingPhase& binding_phase,
        const PassInfoAnalysis& pass_info_analysis,
        const ExecutionReorderPhase& reorder_phase,     // 为将来split barrier的邻接判定预留
        const BarrierGenerationConfig& config = {});
    ~BarrierGenerationPhase() override = default;

    void reset_for_frame() override;
    void on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor) override;

    // 下游查询：pass的屏障批次（无键即无屏障）
    const std::vector<BarrierBatch>* get_pass_barrier_batches(RDGPassNodeRef pass) const;
    const BarrierGenerationResult& get_result() const { return result_; }

    const void debug_info()const;

private:
    void generate_transition_barriers(RDGDependencyGraphRef graph);

    // 处理单条访问记录：跟踪状态更新 + 按需生成转移屏障（after_pass决定发射位置语义）
    void process_access(RDGPassNodeRef pass, const ResourceAccessInfo& access, uint32_t target_queue, bool after_pass);

    // texture状态跟踪器：states 按 [mip * array_layers + layer] 展开
    struct TextureStateTracker
    {
        std::vector<RHIResourceState> states;
        RDGPassNodeRef last_pass = nullptr;    // 上一次触碰的pass（跨队列判定用）
        uint32_t mip_levels = 1;
        uint32_t array_layers = 1;

        inline size_t index(uint32_t mip, uint32_t layer) const { return static_cast<size_t>(mip) * array_layers + layer; }
    };

    // buffer状态跟踪器
    struct BufferStateTracker
    {
        RDGPassNodeRef last_pass = nullptr;
        RHIResourceState state;
    };

private:
    BarrierGenerationConfig config_;

    const CrossQueueSyncAnalysis& sync_analysis_;
    const PassBindingPhase& binding_phase_;            // 保证运行顺序在阶段6之后（initState已就绪）
    const PassInfoAnalysis& pass_info_analysis_;
    const ExecutionReorderPhase& reorder_phase_;

    BarrierGenerationResult result_;

    // 工作数据
    std::unordered_map<RDGTextureNodeRef, TextureStateTracker> texture_trackers_;
    std::unordered_map<RDGBufferNodeRef, BufferStateTracker> buffer_trackers_;
};
