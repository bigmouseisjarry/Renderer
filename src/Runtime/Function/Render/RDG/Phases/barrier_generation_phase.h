#pragma once

#include "IRenderGraphPhase.h"
#include "pass_info_analysis.h"
#include "pass_binding_phase.h"
#include "schedule_reorder.h"
#include "cross_frame_registry.h"   // RHISemaphoreRef + 时间线双signal模型

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// 屏障类型
enum class EBarrierType : uint8_t
{
    ResourceTransition = 0,     // 状态转移屏障（含跨队列转移）
};

// RDG屏障——虚拟屏障：持有资源节点指针，发射时由执行期解析物理对象
struct RDGBarrier
{
    RDGResourceNodeRef resource = nullptr;
    EBarrierType type = EBarrierType::ResourceTransition;

    RDGPassNodeRef source_pass = nullptr;   // 上一次触碰该资源的pass
    RDGPassNodeRef target_pass = nullptr;   // 本次访问的pass（屏障挂在该pass的batch）
    uint32_t source_queue = 0;              // 队列索引（多队列提交期使用）
    uint32_t target_queue = 0;

    // 队列族所有权转移（release/acquire对，仅纹理跨族时非IGNORED；buffer恒IGNORED——信号量覆盖可见性）
    uint32_t src_family = RHI_QUEUE_FAMILY_IGNORED;
    uint32_t dst_family = RHI_QUEUE_FAMILY_IGNORED;

    RHIResourceState before_state = RESOURCE_STATE_UNDEFINED;
    RHIResourceState after_state = RESOURCE_STATE_UNDEFINED;

    // 着色器频率（发射期细化屏障管线阶段）：src=前次使用的频率（tracker账本），dst=本次访问的频率
    // （access.frequency，反射descriptor边有真值）。非反射边/悬空release（上帧末使用未知）保持ALL保守
    uint32_t src_frequency = SHADER_FREQUENCY_ALL;
    uint32_t dst_frequency = SHADER_FREQUENCY_ALL;

    // 发射位置：转移屏障一律挂访问pass之前
    // 唯一例外 QFO release挂生产者pass之后（纯所有权转移不是布局转移）
    bool after_pass = false;

    bool is_subresource = false;            // 是否为子范围访问（非整个资源）
    // 纹理子资源范围（已解析为具体数值，0不做默认解释）
    TextureSubresourceRange subresource = {};
    // buffer范围
    uint32_t buffer_offset = 0;
    uint32_t buffer_size = 0;
};

// 该pass的一个类型屏障批次
struct BarrierBatch
{
    std::vector<RDGBarrier> barriers;
    EBarrierType batch_type = EBarrierType::ResourceTransition;
};

// 结构体A：Phase 7为每个调度节点（pass/悬空起点）产出的同步载荷。
// waits = 该节点须等待的逐时间线包含集（每时间线已取max）；signal = 本队列时间线 + 分配值。
// 值域规则：signal = wait + 1（实现为max(wait[本时间线], B[本时间线])+1并回写B——B见
// RDGCrossFrameRegistry，管跨帧值序[时间线槽复用]与帧内值序[空wait pass]两职责，规则①）
struct RDGSyncNode
{
    // wait条目：逐时间线{包含集值, 贡献者(state,frequency)对}。
    // usages由fold期各access的现成单值累积（state去重/frequency按位OR——推导对frequency线性，
    // 合并精确）；空=该线含未知贡献者（悬空起点registry种子）→Phase 8对该线回退宽掩码
    struct WaitEntry
    {
        RHISemaphoreRef timeline;
        uint64_t value = 0;
        std::vector<std::pair<RHIResourceState, uint32_t>> usages;
    };
    std::vector<BarrierBatch> batches;    // 屏障批次：本pass的转移/acquire + 挂上来的release/归巢（双通道合并后唯一载体）
    std::vector<WaitEntry> waits;                                       // 逐时间线wait包含集
    std::pair<RHISemaphoreRef, uint64_t> signal = { nullptr, 0 };    // 本队列时间线 + signal值
};



struct BarrierGenerationResult
{
    // pass → 同步节点（时间线双signal模型）：node->batches为该pass的屏障批次（本pass转移/
    // acquire + 挂尾的release/归巢），node->waits/signal为同步载荷——双通道已合并（旧
    // pass_barrier_batches通道删除，屏障数据统一走本表）
    std::unordered_map<RDGPassNodeRef, RDGSyncNode> pass_sync_nodes;

    // 悬空起点：族 → 聚合虚拟节点。
    std::unordered_map<uint32_t, RDGSyncNode> dangling_starts;

    // 统计
    uint32_t total_barriers = 0;
};

struct BarrierGenerationConfig
{
    bool enable_debug_output = false;
};

// 阶段 7: 屏障生成阶段
//
// 每资源一个 subresource 级状态跟踪器（纹理按 mipLevels*arrayLayers 展开），
// 初始状态 = imported 声明的 initState / created 资源的池化跨帧状态（阶段6分配结果），
// 按 Phase 2 拓扑序遍历每个pass的 resource_accesses 生成转移屏障：
//   先处理非output访问（屏障在pass之前发射），再处理output访问（屏障在pass之后发射），
//   与旧路径 输入屏障 → pass → 输出屏障 的时序一致
//
// 族感知发射规则（多队列）：
//   同队列        → 状态变化 / UAV→UAV（写后读可见性）时发转移屏障
//   跨队列同族    → timeline信号量wait已建立执行+内存依赖，仅状态变化时发转移屏障（消费者侧）
//   跨队列异族    → EXCLUSIVE纹理：生产者队列release（pass后）+ 消费者队列acquire（pass前）；
//                   buffer族恒IGNORED，退化为同族规则（信号量覆盖可见性）
//   帧首异族      → 池族≠目标族时acquire收回所有权（上一帧由fence隔断=隐式释放）
//
// Present/Copy 的pass内动态屏障（swapchain PRESENT循环、generateMip的mip链屏障）
// 仍由执行期手工发射——它们发生在pass命令之间，访问记录粒度表达不了
//
class BarrierGenerationPhase : public IRenderGraphPhase
{
public:
    BarrierGenerationPhase(
        const QueueSchedule& queue_schedule,
        const PassBindingPhase& binding_phase,
        const PassInfoAnalysis& pass_info_analysis,
        const ExecutionReorderPhase& reorder_phase,     // 为将来split barrier的邻接判定预留
        RDGCrossFrameRegistry& cross_frame_registry,    // 时间线双signal模型
        const BarrierGenerationConfig& config = {});
    ~BarrierGenerationPhase() override = default;

    void reset_for_frame() override;
    void on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor) override;

    // 下游查询：pass的同步节点（无键即无资源访问；屏障批次在node->batches，无屏障即空）
    const RDGSyncNode* get_pass_sync_node(RDGPassNodeRef pass) const;
    const std::unordered_map<uint32_t, RDGSyncNode>& get_dangling_starts() const { return result_.dangling_starts; }
    // 资源本帧的最终状态（tracker终态）——Phase 8归还池时记录此值而非边声明状态：
    // 池化跨帧状态必须与GPU实际布局一致，否则下一帧initState错→漏屏障→layout错位
    RHIResourceState get_final_texture_state(RDGTextureNodeRef texture) const;
    // tracker终态族（池化悬空承接制：release_at_last_use回写真末族进池条目——下帧首触
    // 同族零转移/跨族由悬空起点承接，见on_execute归巢注释）
    uint32_t get_final_texture_family(RDGTextureNodeRef texture) const;
    RHIResourceState get_final_buffer_state(RDGBufferNodeRef buffer) const;
    const BarrierGenerationResult& get_result() const { return result_; }

    const void debug_info()const;

private:
    void generate_transition_barriers(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor);

    // —— 时间线双signal模型：走查三步 ——
    // ⓪走查前：悬空起点预构建
    void prebuild_dangling_starts(PerFrameCommonResourceRef executor);
    // ①屏障两阶段前：聚合依赖资源的逐时间线wait包含集（同族/异族选值）+ signal分配
    RDGSyncNode build_sync_node(RDGPassNodeRef pass, const PassResourceInfo* res_info,uint32_t target_queue, PerFrameCommonResourceRef executor);
    // ②屏障两阶段后：signal已定，按read（只推异族）/write（推双值）推进tracker
    void advance_sync_trackers(const PassResourceInfo* res_info, uint32_t target_queue,uint64_t signal, PerFrameCommonResourceRef executor);
    // ③帧末：import资源tracker终态回写registry（未触键靠registry merge换代保留旧记录）
    void write_back_sync_records(RDGDependencyGraphRef graph);

    // 处理单条访问记录：跟踪状态更新 + 按需生成转移屏障
    void process_access(RDGPassNodeRef pass, const ResourceAccessInfo& access, uint32_t target_queue);

    // 屏障发射统一入口：确保Transition批存在→入队→计数。
    void emit_barrier(RDGSyncNode& node, RDGBarrier barrier);

    // texture状态跟踪器：states 按 [mip * array_layers + layer] 展开
    struct TextureStateTracker
    {
        std::vector<RHIResourceState> states;
        // 频率账本（S6）：与states平行展开，记"到达当前态的那次使用"的shader频率——
        // 下一次转移屏障的src_frequency。初值/跨帧种子=ALL保守（上帧末使用频率不入registry）
        std::vector<uint32_t> state_frequencies;
        RDGPassNodeRef last_pass = nullptr;    // 上一次触碰的pass（跨队列判定用）
        uint32_t mip_levels = 1;
        uint32_t array_layers = 1;
        uint32_t owner_family = RHI_QUEUE_FAMILY_IGNORED;   // 所有权追踪（旧屏障模型维护，勿与新模型混用）

        uint32_t sync_queue = UINT32_MAX;     // 种子挂靠队列（registry命中=记录owner/悬空挂靠；走查中被推进改写）
        // 时间线对象 → {同族signal, 异族signal}：帧首从registry seed（import），访问按read/write推进。
        // 上帧旧时间线记录保留——下帧wait它=同槽fence语义（已signal瞬间通过）。键持ref跨帧保活。
        // 悬空+1落账由prebuild_dangling_starts在走查前patch进异族槽
        std::unordered_map<RHISemaphoreRef, std::pair<uint64_t, uint64_t>> sync_signals;

        inline size_t index(uint32_t mip, uint32_t layer) const { return static_cast<size_t>(mip) * array_layers + layer; }
    };

    // buffer状态跟踪器
    struct BufferStateTracker
    {
        RDGPassNodeRef last_pass = nullptr;
        RHIResourceState state;
        uint32_t state_frequency = SHADER_FREQUENCY_ALL;    // 频率账本（S6）：同TextureStateTracker::state_frequencies

        // —— 时间线双signal模型：与纹理同构；族跟随末触pass（sync_queue，1:1），
        // 无所有权概念→无悬空起点/无release-acquire ——
        uint32_t sync_queue = UINT32_MAX;
        std::unordered_map<RHISemaphoreRef, std::pair<uint64_t, uint64_t>> sync_signals;
    };

    // —— 子范围分组——
    // 访问范围内按(state, frequency)等价类归组：每组=发射最小单位（统一before态+账本频率，矩形合并后的ranges逐矩形发射）
    struct TextureSubrangeGroup
    {
        RHIResourceState state = RESOURCE_STATE_UNDEFINED;
        uint32_t frequency = SHADER_FREQUENCY_ALL;
        std::vector<TextureSubresourceRange> ranges;    // 矩形合并产物（aspect由发射侧统一填）
    };
    // 全齐短路→整范围单组单矩形（常规路径条数与旧整范围路径一致）；混合→行扫描+竖向融合
    std::vector<TextureSubrangeGroup> compute_texture_subrange_groups(
        const TextureStateTracker& tracker, const ResourceAccessInfo& access) const;

private:
    BarrierGenerationConfig config_;

    const QueueSchedule& queue_schedule_;              // 调度序（执行序基准）+pass→队列/队列→族查询
    const PassBindingPhase& binding_phase_;            // 保证运行顺序在阶段6之后（initState已就绪）
    const PassInfoAnalysis& pass_info_analysis_;
    const ExecutionReorderPhase& reorder_phase_;
    RDGCrossFrameRegistry& cross_frame_registry_;     // 跨帧持久（RenderSystem拥有，注入）——B与import同步记录

    BarrierGenerationResult result_;

    std::unordered_map<RDGTextureNodeRef, TextureStateTracker> texture_trackers_;
    std::unordered_map<RDGBufferNodeRef, BufferStateTracker> buffer_trackers_;
};
