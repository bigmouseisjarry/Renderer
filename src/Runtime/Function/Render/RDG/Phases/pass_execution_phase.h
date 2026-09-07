#pragma once

#include "IRenderGraphPhase.h"
#include "queue_schedule.h"
#include "schedule_reorder.h"
#include "cross_queue_sync_analysis.h"
#include "barrier_generation_phase.h"
#include "pass_binding_phase.h"

#include <atomic>
#include <string>
#include <unordered_map>
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
    uint32_t total_sync_points_processed = 0;   // 本帧实际消费的有效同步点数（主线程单点赋值）
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
//   Step B 提交批次切分：同步点驱动——pass是consumer则开启新批次（wait必须挂批次首pass，
//          否则批内后续pass会在wait前执行）；pass是producer则作为该批次的结尾（signal挂批次末尾）；
//          串行边界处强制切批（串行区归主线程录制）。切分覆盖整个流（含串行区）。
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
        const CrossQueueSyncAnalysis& sync_analysis,
        const BarrierGenerationPhase& barrier_generation_phase,
        const PassBindingPhase& binding_phase,
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
        bool hasPresent = false;                    // 含Present pass（signal finishSemaphore）
        std::vector<RHICommandListRef> commandLists;    // 批内piece的命令流（数组序=执行序）
    };

    // 录制piece：批内并行chunk的最小单位——队列流内连续段（不跨批次），独占一条命令流
    struct RecordPiece
    {
        uint32_t queue = 0;
        uint32_t batchIndex = 0;    // 所属批次（batches_[queue]下标）——回填commandLists用
        uint32_t begin = 0;
        uint32_t end = 0;
    };

    // ============================ 规划步骤 ============================

    // Step A：队列流 + 串行边界 + 有效同步点过滤（填充 streams_/activeQueues_/orderedPasses_）
    void build_queue_streams(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor);
    // Step B：同步点驱动的批次切分（填充 batches_）
    void split_submit_batches();
    // Step C：提交计划组装 + timeline值按提交序分配（写入 executor->submitPlan）
    void build_submit_plan(PerFrameCommonResourceRef executor);
    // Step D：piece均衡分配 + workers并行录制 + 主线程串行区录制 + 回填计划
    void record_all(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor);

    // ============================ 录制 ============================

    // 录制队列流 [beginIndex, endIndex) 的pass到指定命令流
    void record_stream_range(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor,
                             uint32_t queueIndex, const QueueStream& stream,
                             uint32_t beginIndex, uint32_t endIndex, RHICommandListRef command);
    void execute_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor,
                      uint32_t queueIndex, RDGPassNodeRef pass, RHICommandListRef command);

    // 按类型执行（queueIndex用于组装RDGPassContext的队列信息）
    void execute_render_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, uint32_t queueIndex, RDGRenderPassNodeRef pass, RHICommandListRef command);
    void execute_compute_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, uint32_t queueIndex, RDGComputePassNodeRef pass, RHICommandListRef command);
    void execute_ray_tracing_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, uint32_t queueIndex, RDGRayTracingPassNodeRef pass, RHICommandListRef command);
    void execute_present_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGPresentPassNodeRef pass, RHICommandListRef command);
    void execute_copy_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGCopyPassNodeRef pass, RHICommandListRef command);

    // 屏障与同步
    void emit_barrier(RHICommandListRef command, const RDGBarrier& barrier);   // 单条虚拟屏障→物理命令
    // after = false：发射非output边的转移屏障（pass之前）；after = true：发射output边的收敛屏障（pass之后）
    void insert_pass_barriers(RHICommandListRef command, RDGPassNodeRef pass, bool after);
    // pass的屏障批次中是否含帧首跨族acquire标记（source_pass==nullptr且src_family有效）——
    // 该pass所在批次需wait graphics prologueDone
    bool pass_has_frame_start_acquire(RDGPassNodeRef pass) const;
    // pass之前是否挂有跨族acquire屏障（release/acquire对，含帧内与帧首）——验证层要求配对release
    // 在acquire之前【提交】，故该pass必须开启新批次（批次首），保证其生产者批次（含release，
    // 且生产者被规则收批于批尾）在提交序中严格在前
    bool pass_has_cross_family_acquire(RDGPassNodeRef pass) const;
    // pass之后是否挂有跨族release屏障——该pass后收批（批次尾），使其批次的排序键=canonical(该pass)
    bool pass_has_cross_family_release(RDGPassNodeRef pass) const;

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
    const CrossQueueSyncAnalysis& sync_analysis_;
    const BarrierGenerationPhase& barrier_generation_phase_;
    const PassBindingPhase& binding_phase_;

    CommandRecordingResult recording_result_;

    // ============================ 帧内工作数据（on_execute期间有效；workers在其内join） ============================

    std::vector<RDGPassNodeRef> orderedPasses_;             // 全局拓扑序（剔除culled），释放sweep用
    std::unordered_map<RDGPassNodeRef, uint32_t> topoIndex_;// pass→orderedPasses_下标（批次排序用）
    std::vector<QueueStream> streams_;                      // 每队列流
    std::vector<uint32_t> activeQueues_;                    // 有pass的队列下标
    std::vector<std::vector<StreamBatch>> batches_;         // 每队列的提交批次
    std::vector<std::vector<uint32_t>> planBatchIndex_;     // (q,b)→计划批次下标（提交序=拓扑序）

    std::vector<CrossQueueSyncPoint> activePoints_;         // 有效同步点（悬空已过滤）
    std::unordered_map<RDGPassNodeRef, std::vector<uint32_t>> producerPoints_;   // pass→其作为producer的点
    std::unordered_map<RDGPassNodeRef, std::vector<uint32_t>> consumerPoints_;   // pass→其作为consumer的点
    std::unordered_map<uint32_t, uint64_t> pointValues_;    // 同步点下标→timeline值（提交序分配）
};
