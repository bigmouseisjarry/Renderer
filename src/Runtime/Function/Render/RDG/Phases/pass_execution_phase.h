#pragma once

#include "IRenderGraphPhase.h"
#include "queue_schedule.h"
#include "schedule_reorder.h"
#include "barrier_generation_phase.h"
#include "pass_binding_phase.h"
#include "cross_frame_registry.h"

#include <atomic>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// 命令录制配置
struct CommandRecordingConfig
{
    bool enable_debug_markers = true;       // PushEvent/PopEvent GPU调试标记
    bool enable_debug_output = false;

    // chunked并行录制预算 = ANY池worker数(chunk_count-1) + 主线程(1)。
    // 多队列下预算跨全队列按pass数动态均衡：各队列预串行流按提交批次切成piece
    // （piece不跨批次——批次是vkQueueSubmit的边界），最重的队列可占多个piece；
    // 串行尾（Editor UI起的全局拓扑后缀）各队列自己的部分由主线程在join后录制。
    // chunk_count亦是每队列预建命令流数（worker piece数+1条串行尾）
    uint32_t chunk_count = 3;

    // 必须串行录制的pass名（如"Editor UI"：ImGui全局态 + 编辑器UI直写各pass的setting成员，与并发录制存在写侧竞态）。
    // 首个命中位置之后的全部pass归入串行区，主线程join后录制
    std::vector<std::string> serial_pass_names = { "Editor UI" };

    // GPU时间戳打点开关（编译侧）。与RHIRenderQueryInfo::enable_gpu_timing是两个独立结构体——
    // 录制期以"本开关 && 每帧传入的RHIRenderQuery非空"共同判定（见record_stream_range）
    bool enable_gpu_timing = false;
};

// 命令录制结果（计数在worker上并发自增，用原子）
struct CommandRecordingResult
{
    std::atomic<uint32_t> total_passes_executed = 0;
    std::atomic<uint32_t> total_barriers_inserted = 0;
    uint32_t total_submit_batches = 0;          // 提交批次数（含join批）
};

// 阶段 8: Pass执行阶段——多队列GPU执行层，替代 RDGBuilder::Execute()
//
// 流水线（主线程规划 → 并行录制 → SubmitRHI消费计划）：
// 
//   Step A 队列流构建：Phase 3 的 queue_schedules 剔除culled（流内保持拓扑序），
//          过滤悬空同步点（生产者/消费者被cull），建 producer/consumer 反查表；
//          串行边界 = 全局拓扑序中首个命中 serial_pass_names 的位置（各队列流内的后缀映射）
// 
//   Step B 提交批次切分：统一守卫模型——pass的批边界只由"未覆盖的守卫"决定。
//          守卫 = consumer点（须等某跨队列生产者的提交批完成）∪ 帧内跨族acquire（其配对release
//          须先提交且先执行），每个守卫=另一队列上的一个canonical位置；帧首acquire的配对release
//          在prologue批（恒为计划首）不构成守卫。两类守卫的覆盖判据【正交，不可互换】：
//            consumer守卫（数据就绪）只能由支配覆盖——本队列更早批已等过该源队列上≥守卫位置的
//              生产者 ⇒ 队列串行 ⇒ 已完成（跨队列提交序不蕴含执行序）
//            acquire守卫（QFO配对序）只能由提交序覆盖——批头canonical>守卫位置 ⇒ 本批提交严格
//              在后；其执行序由同pass的consumer点蕴含（蕴含断言+SSIS取最大）
//          （旧acquire开批规则被蕴含；release收批删除——配对序由acquire侧检查独立保证；
//          producer收批保留：signal挂批尾的粒度语义）。串行边界处强制切批，切分覆盖整个流。
//          批次级无环证明：环上各信号量边给出 topo(p0)<topo(c0)≤topo(p1)<...<topo(p0) 矛盾
// 
//   Step C 提交计划组装：两遍按提交序（队列序×批次序）遍历——
//          第一遍分配timeline值（signal值必须按提交序严格递增），含每队列末批的queueDone；
//          第二遍组装waits（consumer点的值+to_state推导stage）/signals；
//          graphics首批次wait startSemaphore（swapchain acquire），Present批次signal finishSemaphore；
//          多队列时追加空join批（等待全部queueDone → signal帧槽fence），单队列时fence挂末批
// 
//   Step D 并行录制：预串行批次的piece跨全队列按pass数动态均衡分给workers（AddQueuedWork
//          帧号stamping使lambda内ThreadFrameIndex()正确），WaitIdle join后主线程录串行批次，
//          最后把各批次的命令流按序回填进提交计划
//
// 每个pass的录制流：PushEvent → before屏障（阶段7）→ 按类型分派执行用户lambda →
//   after屏障 → PopEvent；release/acquire屏障（阶段7跨族）自动随生产者/消费者pass的命令流发射。
//   last-use释放不在录制期做（RDG池无锁，不能并发）——join后由release_sweep按拓扑全序统一执行
//
// Present/Copy 保留其pass内手工屏障（swapchain PRESENT 循环、generateMip 的 mip 链屏障）。
//
// 退化路径：QueueScheduleConfig 关闭 async_compute/copy_queue → 全部pass落graphics →
// 无同步点、无join批，行为等价旧单流提交（外加少量小提交）
//
// 顺序一致性注意：CrossQueueSyncAnalysis 内部的 pass_local_to_queue_indices_ 基于 Phase 3 的
// queue_schedules（重排前顺序）。当前重排在 schedule_reorder.cpp 中被 if(false) 关闭，
// 与 optimized_timeline 相同，无害；未来开启重排时需统一改为消费 optimized_timeline。
//
class PassExecutionPhase : public IRenderGraphPhase
{
public:
    PassExecutionPhase(
        const QueueSchedule& queue_schedule,
        const ExecutionReorderPhase& reorder_phase,
        const BarrierGenerationPhase& barrier_generation_phase,
        const PassBindingPhase& binding_phase,
        RDGCrossFrameRegistry& cross_frame_registry,
        const CommandRecordingConfig& config = {});
    ~PassExecutionPhase() override = default;

    void reset_for_frame() override;
    void on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor) override;

    const CommandRecordingResult& get_result() const { return recording_result_; }

private:
    // 队列流：下标=队列下标（与QueueSchedule::all_queues对齐）
    struct QueueStream
    {
        std::vector<RDGPassNodeRef> passes;      // 队列内拓扑序（剔除culled）
        uint32_t serialPos = UINT32_MAX;        // 串行区起点下标（UINT32_MAX=本流无串行区）
    };

    // 提交批次：一次vkQueueSubmit，pass区间为队列流内连续段 [begin,end)
    struct StreamBatch
    {
        uint32_t begin = 0;
        uint32_t end = 0;
        bool serial = false;                        // 串行区批次（主线程join后录制）
        bool isDanglingPrologue = false;            // 悬空prologue空批（只含该族帧首release barriers，
                                                    // 主线程录制；不与任何pass共批——见Step B注释）
        bool hasPresent = false;                    // 含Present pass（signal finishSemaphore）
        const char* openReason = "stream-start";    // 开批原因（仅StepB日志标注用）
        std::vector<RHICommandListRef> commandLists;    // 批内piece的命令流（数组序=执行序）
    };

    // 录制piece：提交批次内的并行chunk的最小单位——队列流内连续段（不跨批次），独占一条命令流
    struct RecordPiece
    {
        uint32_t queue = 0;
        uint32_t batchIndex = 0;    // 所属批次（batches_[queue]下标）——回填commandLists用
        uint32_t begin = 0;
        uint32_t end = 0;
    };

    // Step A：队列流 + 串行边界（填充 streams_/activeQueues_/orderedPasses_；同步点体系已随
    // 时间线双signal模型退役，CrossQueueSyncAnalysis仅存活于差分审计）
    void build_queue_streams(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor);
    // Step B：值级守卫的批次切分（填充 batches_）——pass的结构体A waits已带具体值，
    // 未被waitedMax支配覆盖的wait开批发wait；本队列时间线的wait恒开批（规则③防同批自等）
    void split_submit_batches(PerFrameCommonResourceRef executor);
    // Step C：提交计划组装（写入 executor->submitPlan）——零值分配（wait/signal全由Phase 7定稿），
    // 只做聚合：批waits=批内逐时间线max+支配去重；每批每时间线恰一条signal=批内最大值（规则①）
    void build_submit_plan(PerFrameCommonResourceRef executor);
    // Step D：piece均衡分配 + workers并行录制 + 主线程串行区录制 + 回填计划
    void record_all(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor);

    // 悬空起点辅助：q是否为某悬空起点的挂靠队列（挂靠判定=时间线对象匹配）；
    // 取该队列的悬空节点（无则nullptr）
    bool queue_has_dangling(uint32_t q, PerFrameCommonResourceRef executor) const;
    const RDGSyncNode* queue_dangling_node(uint32_t q, PerFrameCommonResourceRef executor) const;

    // 录制队列流 [beginIndex, endIndex) 的pass到指定命令流
    void record_stream_range(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor,
                             uint32_t queueIndex, const QueueStream& stream,
                             uint32_t beginIndex, uint32_t endIndex, RHICommandListRef command);
    void execute_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor,
                      uint32_t queueIndex, RDGPassNodeRef pass, RHICommandListRef command);

    // 按类型执行
    void execute_render_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, uint32_t queueIndex, RDGRenderPassNodeRef pass, RHICommandListRef command);
    void execute_compute_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, uint32_t queueIndex, RDGComputePassNodeRef pass, RHICommandListRef command);
    void execute_ray_tracing_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, uint32_t queueIndex, RDGRayTracingPassNodeRef pass, RHICommandListRef command);
    void execute_present_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGPresentPassNodeRef pass, RHICommandListRef command);
    void execute_copy_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGCopyPassNodeRef pass, RHICommandListRef command);

    // 屏障与同步
    void emit_barrier(RHICommandListRef command, const RDGBarrier& barrier);   // 单条虚拟屏障→物理命令
    // after = false：发射非output边的转移屏障（pass之前）；after = true：发射output边的收敛屏障（pass之后）
    void insert_pass_barriers(RHICommandListRef command, RDGPassNodeRef pass, bool after);

    // 收集pass的帧内跨族acquire守卫（{配对release所在pass, release队列}）——QFO配对序断言用
    //（审计②）。帧首acquire（source_pass==nullptr）跳过——其配对release在悬空起点prologue批
    //（sortKey强制0=计划首），提交序天然覆盖
    void collect_in_frame_acquire_guards(RDGPassNodeRef pass,
        std::vector<std::pair<RDGPassNodeRef, uint32_t>>& guards) const;

    // 释放：录制期不碰任何池，全部录制完成后按拓扑全序统一执行
    void release_sweep(RDGDependencyGraphRef graph, const std::vector<RDGPassNodeRef>& orderedPasses);
    void release_at_last_use(RDGDependencyGraphRef graph, RDGPassNodeRef pass);
    void release_texture(RDGTextureNodeRef textureNode, RHIResourceState state, uint32_t queueFamily);
    void release_buffer(RDGBufferNodeRef bufferNode, RHIResourceState state);
    void release_pooled_descriptor_sets(RDGDependencyGraphRef graph);

    // 工具
    std::array<RHIDescriptorSetRef, MAX_DESCRIPTOR_SETS> build_descriptor_array(RDGPassNodeRef pass) const;

private:
    CommandRecordingConfig config_;

    const QueueSchedule& queue_schedule_;
    const ExecutionReorderPhase& reorder_phase_;
    const BarrierGenerationPhase& barrier_generation_phase_;
    const PassBindingPhase& binding_phase_;
    RDGCrossFrameRegistry& cross_frame_registry_;   // 跨帧持久状态（RenderSystem拥有，注入；见其类注释）

    CommandRecordingResult recording_result_;

    // ============================ 帧内工作数据（on_execute期间有效；workers在其内join） ============================

    std::vector<RDGPassNodeRef> orderedPasses_;             // 经过剔除后的pass全局拓扑序，释放sweep用
    std::unordered_map<RDGPassNodeRef, uint32_t> topoIndex_;// pass在orderedPasses_中的下标位置（批次排序用）
    std::vector<QueueStream> streams_;                      // 每队列流
    std::vector<uint32_t> activeQueues_;                    // 有pass（或有悬空起点挂靠）的队列下标
    std::vector<std::vector<StreamBatch>> batches_;         // 每队列的提交批次
    std::vector<std::vector<uint32_t>> planBatchIndex_;     // (q,b)→计划批次下标（提交序=拓扑序）
};
