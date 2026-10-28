#pragma once

#include "IRenderGraphPhase.h"
#include "pass_info_analysis.h"
#include "pass_binding_phase.h"
#include "schedule_reorder.h"
#include "cross_frame_registry.h"   // RHISemaphoreRef + 时间线双signal模型（刀1注入，刀2起消费）

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

// 该pass的一个类型屏障批次
struct BarrierBatch
{
    std::vector<RDGBarrier> barriers;
    EBarrierType batch_type = EBarrierType::ResourceTransition;
};

// 结构体A（刀1定义，刀2起填充/消费）：Phase 7为每个调度节点（pass/悬空起点）产出的同步载荷。
// waits = 该节点须等待的逐时间线包含集（每时间线已取max）；signal = 本队列时间线 + 分配值。
// 值域规则：signal = wait + 1（实现为max(wait[本时间线], B[本时间线])+1并回写B——B见
// RDGCrossFrameRegistry，管跨帧值序[时间线槽复用]与帧内值序[空wait pass]两职责，规则①）
struct RDGSyncNode
{
    std::vector<BarrierBatch> batches;    // 屏障批次：本pass的转移/acquire + 挂上来的release/归巢（双通道合并后唯一载体）
    std::vector<std::pair<RHISemaphoreRef, uint64_t>> waits;         // 逐时间线wait包含集
    std::pair<RHISemaphoreRef, uint64_t> signal = { nullptr, 0 };    // 本队列时间线 + signal值
};



struct BarrierGenerationResult
{
    // pass → 同步节点（时间线双signal模型）：node->batches为该pass的屏障批次（本pass转移/
    // acquire + 挂尾的release/归巢），node->waits/signal为同步载荷——双通道已合并（旧
    // pass_barrier_batches通道删除，屏障数据统一走本表）
    std::unordered_map<RDGPassNodeRef, RDGSyncNode> pass_sync_nodes;

    // 悬空起点（刀1定义，刀2起填充/消费）：族 → 聚合虚拟节点。
    // import资源本帧首触跨族时，资源所在族须在帧首执行release（理解：与资源同队列的虚假pass
    // 对资源做read——只推资源该队列时间线的异族signal，+1落账进tracker记录而非临时计算）。
    // waits = 相关资源registry记录的逐时间线max包含集（保证release排在上帧末触之后）；
    // barriers可为空 = 本帧不存在import首触跨族（池化initFamily跨族首触同走此框架，waits为空
    // ——同槽fence已保证隔帧完成）。刀2起由Phase 8作为各族prologue批消费（取代frame_start_releases_
    // 与prologueDone守卫；import帧末归巢删除，池化归巢保留为纯barrier）
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
// 替代旧路径的 CreateInputBarriers/CreateOutputBarriers/PreviousState 内联扫描：
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
        RDGCrossFrameRegistry& cross_frame_registry,    // 时间线双signal模型（刀1注入，刀2起消费：B分配/import记录种子与回写）
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

    // —— 时间线双signal模型（刀2a）：走查三步 ——
    // ⓪走查前（刀A）：悬空起点预构建——存在性/成员/signal低位预分配/waits/帧首release/+1落账
    //   全部定格。病根修复：旧形态悬空signal在走查中段按B分配（值高于早段pass），Phase 8并入
    //   队首b0发射后，前缀max去重把"值低于悬空signal的后段批"（帧首拷贝族）静音——等待者被b0
    //   的发射提前放行，跨队列WAR/WAW蒸发（syncval 980条hazard案，2026-10-28）。预分配构造性
    //   保证悬空值<本帧该线一切pass值：b0发射=自家工作max，后批各发各的signal
    void prebuild_dangling_starts(PerFrameCommonResourceRef executor, uint32_t graphics_family);
    // ①屏障两阶段前：聚合依赖资源的逐时间线wait包含集（同族/异族选值）+ signal分配
    RDGSyncNode build_sync_node(RDGPassNodeRef pass, const PassResourceInfo* res_info,
        uint32_t target_queue, PerFrameCommonResourceRef executor);
    // ②屏障两阶段后：signal已定，按read（只推异族）/write（推双值）推进tracker
    void advance_sync_trackers(const PassResourceInfo* res_info, uint32_t target_queue,
        uint64_t signal, PerFrameCommonResourceRef executor);
    // ③帧末：import资源tracker终态回写registry（未触键靠registry merge换代保留旧记录）
    void write_back_sync_records(RDGDependencyGraphRef graph);

    // 处理单条访问记录：跟踪状态更新 + 按需生成转移屏障（after_pass决定发射位置语义）
    void process_access(RDGPassNodeRef pass, const ResourceAccessInfo& access, uint32_t target_queue, bool after_pass);

    // texture状态跟踪器：states 按 [mip * array_layers + layer] 展开
    struct TextureStateTracker
    {
        std::vector<RHIResourceState> states;
        RDGPassNodeRef last_pass = nullptr;    // 上一次触碰的pass（跨队列判定用）
        uint32_t mip_levels = 1;
        uint32_t array_layers = 1;
        uint32_t owner_family = RHI_QUEUE_FAMILY_IGNORED;   // 所有权追踪（旧屏障模型维护，勿与新模型混用）

        // —— 时间线双signal模型（刀2a；刀A瘦身）——
        // registry键/import身份不落tracker：回写时问node->IsImported()+binding_phase现取键；
        // "本帧已触"由last_pass非空蕴含（sync_key/sync_touched已删，2026-10-28刀A）
        uint32_t sync_queue = UINT32_MAX;     // 种子挂靠队列（registry命中=记录owner/悬空挂靠；走查中被推进改写）
        // 时间线对象 → {同族signal, 异族signal}：帧首从registry seed（import），访问按read/write推进。
        // 上帧旧时间线记录保留——下帧wait它=同槽fence语义（已signal瞬间通过）。键持ref跨帧保活。
        // 悬空+1落账由prebuild_dangling_starts在走查前patch进异族槽（刀A）
        std::unordered_map<RHISemaphoreRef, std::pair<uint64_t, uint64_t>> sync_signals;

        inline size_t index(uint32_t mip, uint32_t layer) const { return static_cast<size_t>(mip) * array_layers + layer; }
    };

    // buffer状态跟踪器
    struct BufferStateTracker
    {
        RDGPassNodeRef last_pass = nullptr;
        RHIResourceState state;

        // —— 时间线双signal模型（刀2a）：与纹理同构；族跟随末触pass（sync_queue，1:1），
        // 无所有权概念→无悬空起点/无release-acquire ——
        uint32_t sync_queue = UINT32_MAX;
        std::unordered_map<RHISemaphoreRef, std::pair<uint64_t, uint64_t>> sync_signals;
    };

private:
    BarrierGenerationConfig config_;

    const QueueSchedule& queue_schedule_;              // 调度序（执行序基准）+pass→队列/队列→族查询
    const PassBindingPhase& binding_phase_;            // 保证运行顺序在阶段6之后（initState已就绪）
    const PassInfoAnalysis& pass_info_analysis_;
    const ExecutionReorderPhase& reorder_phase_;
    RDGCrossFrameRegistry& cross_frame_registry_;     // 跨帧持久（RenderSystem拥有，注入）——B与import同步记录（刀1）

    BarrierGenerationResult result_;

    // 工作数据。tracker按节点各开（2026-10-26回归单节点制：物理图双节点曾在tracker层以物理键
    // 共享兜底，其病根G-Buffer IOR/Gizmo Data双节点已修于图层，共享机制退役；同图双节点现由
    // on_execute的物理指针守卫告警——图层病根图层管）
    std::unordered_map<RDGTextureNodeRef, TextureStateTracker> texture_trackers_;
    std::unordered_map<RDGBufferNodeRef, BufferStateTracker> buffer_trackers_;
};
