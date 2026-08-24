#pragma once

#include "IRenderGraphPhase.h"
#include "queue_schedule.h"
#include "schedule_reorder.h"
#include "cross_queue_sync_analysis.h"
#include "barrier_generation_phase.h"
#include "pass_binding_phase.h"

// 命令录制配置
struct CommandRecordingConfig
{
    bool enable_debug_markers = true;       // PushEvent/PopEvent GPU调试标记
    bool enable_debug_output = false;
};

// 命令录制结果
struct CommandRecordingResult
{
    uint32_t total_passes_executed = 0;
    uint32_t total_barriers_inserted = 0;
    uint32_t total_sync_points_processed = 0;
};

// 阶段 8: Pass执行阶段——最终执行层，替代 RDGBuilder::Execute()
//
// 录制顺序：按 Phase 2 拓扑序单命令流录制（依赖保序的全局序列）。
// 注意：不按 optimized_timeline 分队列桶录制——把各队列时间线整桶串接会违反跨队列依赖
// （消费者在生产者之前录制）。分队列时间线是多队列提交期的产物，届时配合
// optimized_sync_points 的信号量一起消费（process_sync_points 本期只做统计，Sakura亦如此）。
//
// 每个pass的录制流：
//   PushEvent → before屏障（阶段7，非output边状态）→ 按类型分派执行用户lambda →
//   after屏障（阶段7，output边收敛状态）→ last-use释放（阶段6生命期）→ PopEvent
//   对应旧路径 ExecutePass 内 CreateInputBarriers → pass → CreateOutputBarriers → ReleaseResource 的时序
//
// Present/Copy 保留其pass内手工屏障（swapchain PRESENT 循环、generateMip 的 mip 链屏障），
// 通用屏障由阶段7生成，不再调用旧的 CreateInput/OutputBarriers。
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
    void execute_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGPassNodeRef pass);

    // 按类型执行
    void execute_render_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGRenderPassNodeRef pass);
    void execute_compute_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGComputePassNodeRef pass);
    void execute_ray_tracing_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGRayTracingPassNodeRef pass);
    void execute_present_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGPresentPassNodeRef pass);
    void execute_copy_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGCopyPassNodeRef pass);

    // 屏障与同步
    // after = false：发射非output边的转移屏障（pass之前）；after = true：发射output边的收敛屏障（pass之后）
    void insert_pass_barriers(PerFrameCommonResourceRef executor, RDGPassNodeRef pass, bool after);

    // rendering target 准备（原 RDGBuilder::PrepareRenderingTarget）；
    // attachment view 记入 outAttachmentViews，由调用方在 EndRendering 后归还视图池
    void prepare_rendering_target(RDGDependencyGraphRef graph, RDGRenderPassNodeRef pass, RHIRenderingInfo& renderingInfo,
                                  std::vector<RHITextureViewRef>& attachmentViews);

    // 释放
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
