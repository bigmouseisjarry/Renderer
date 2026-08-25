#pragma once

#include "IRenderGraphPhase.h"
#include "queue_schedule.h"
#include "schedule_reorder.h"
#include "cross_queue_sync_analysis.h"
#include "barrier_generation_phase.h"
#include "pass_binding_phase.h"

#include <atomic>
#include <string>
#include <vector>

// 命令录制配置
struct CommandRecordingConfig
{
    bool enable_debug_markers = true;       // PushEvent/PopEvent GPU调试标记
    bool enable_debug_output = false;

    // chunked并行录制：按连续边界把拓扑序pass切成多段，各段并行录进各自的命令流
    // （byPass=true，独占context直接vkCmd），最后由SubmitRHI用ExecuteBatch按序单次提交。
    // 录制顺序≠提交顺序，语义由提交序保证（Phase 6/7前置使每pass录制顺序无关）
    // bool enable_parallel_recording = false;
    uint32_t chunk_count = 3;               // chunk总数 = ANY池worker数(2) + 主线程(1)

    // 必须串行录制的pass名（如"Editor UI"：ImGui全局态 + 编辑器UI直写各pass的setting成员，
    // 与并发录制存在写侧竞态）。首个命中位置之后的全部pass归入最后一个chunk，
    // 由主线程在workers join之后录制，时间上与一切并发录制无重叠
    std::vector<std::string> serial_pass_names = { "Editor UI" };
};

// 命令录制结果（计数在worker上并发自增，用原子）
struct CommandRecordingResult
{
    std::atomic<uint32_t> total_passes_executed = 0;
    std::atomic<uint32_t> total_barriers_inserted = 0;
    uint32_t total_sync_points_processed = 0;   // 主线程单点赋值，无需原子
};

// 阶段 8: Pass执行阶段——最终执行层，替代 RDGBuilder::Execute()
//
// 录制顺序：按 Phase 2 拓扑序（依赖保序的全局序列）。
// 注意：不按 optimized_timeline 分队列桶录制——把各队列时间线整桶串接会违反跨队列依赖
// （消费者在生产者之前录制）。分队列时间线是多队列提交期的产物，届时配合
// optimized_sync_points 的信号量一起消费（process_sync_points 本期只做统计，Sakura亦如此）。
//
// 每个pass的录制流：
//   PushEvent → before屏障（阶段7，非output边状态）→ 按类型分派执行用户lambda →
//   after屏障（阶段7，output边收敛状态）→ PopEvent
//   对应旧路径 ExecutePass 内 CreateInputBarriers → pass → CreateOutputBarriers 的时序。
//   last-use释放不在录制期做（RDG池无锁，不能并发）——join后由release_sweep按拓扑全序统一执行
//
// Present/Copy 保留其pass内手工屏障（swapchain PRESENT 循环、generateMip 的 mip 链屏障），
// 通用屏障由阶段7生成，不再调用旧的 CreateInput/OutputBarriers。
//
// 并行分支（enable_parallel_recording）：worker chunks → AddQueuedWork（帧号stamping自动完成，
// lambda内ThreadFrameIndex()正确）→ WaitIdle(ANY) → 主线程录尾chunk（含串行pass）→ release_sweep
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
    // 核心执行流
    void execute_scheduled_passes(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor);
    void record_parallel(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, const std::vector<RDGPassNodeRef>& orderedPasses);

    // 录制 [beginIndex, endIndex) 区间的pass到指定命令流（串行/并行chunk共用）
    void record_pass_range(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RHICommandListRef command,
                           uint32_t beginIndex, uint32_t endIndex, const std::vector<RDGPassNodeRef>& orderedPasses);
    void execute_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGPassNodeRef pass, RHICommandListRef command);

    // 按类型执行
    void execute_render_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGRenderPassNodeRef pass, RHICommandListRef command);
    void execute_compute_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGComputePassNodeRef pass, RHICommandListRef command);
    void execute_ray_tracing_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGRayTracingPassNodeRef pass, RHICommandListRef command);
    void execute_present_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGPresentPassNodeRef pass, RHICommandListRef command);
    void execute_copy_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGCopyPassNodeRef pass, RHICommandListRef command);

    // 屏障与同步
    // after = false：发射非output边的转移屏障（pass之前）；after = true：发射output边的收敛屏障（pass之后）
    void insert_pass_barriers(RHICommandListRef command, RDGPassNodeRef pass, bool after);

    // 释放：录制期不碰任何池，全部录制完成后按拓扑全序统一执行
    void release_sweep(RDGDependencyGraphRef graph, const std::vector<RDGPassNodeRef>& orderedPasses);
    void release_at_last_use(RDGDependencyGraphRef graph, RDGPassNodeRef pass);
    void release_texture(RDGTextureNodeRef textureNode, RHIResourceState state);
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
};
