#pragma once

#include "IRenderGraphPhase.h"
#include "queue_schedule.h"
#include "schedule_reorder.h"
#include "cross_queue_sync_analysis.h"
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

    // 跨帧同步模式：true=粗链（帧首各队列wait上帧其他队列queueDone，帧间串行、实现最简）；
    // false=细链（imported资源首触批wait其上帧各队列末触点，帧间按资源重叠）。
    // 触碰集/末触signal/注册表回写在两模式下恒做（细链wait的数据源不能因模式切换断代），
    // 仅wait组装侧不同——粗链模式下末触signal成为无人wait的冗余信号（无害）
    bool coarse_cross_frame_sync = false;
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
        const CrossQueueSyncAnalysis& sync_analysis,
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

    // Step A：队列流 + 串行边界 + 有效同步点过滤（填充 streams_/activeQueues_/orderedPasses_）
    void build_queue_streams(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor);
    // Step B：同步点驱动的批次切分（填充 batches_）
    void split_submit_batches();
    // Step C：提交计划组装 + timeline值按提交序分配（写入 executor->submitPlan）
    void build_submit_plan(PerFrameCommonResourceRef executor);
    // Step D：piece均衡分配 + workers并行录制 + 主线程串行区录制 + 回填计划
    void record_all(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor);

    // 细链触碰集推导（build_submit_plan开头调用，需batches_已切好）：
    // imported资源（键=底层RHI对象指针）→ 每键每队列首触批（wait挂点）+ 每键全局末触pass
    // （归巢判定与注册表回写用）。触碰集=图边全集（foreach含虚拟Dependency边）——
    void compute_imported_touch_sets();


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
    // pass的屏障批次中是否含帧首跨族acquire标记（source_pass==nullptr且src_family有效）——
    // 该pass所在批次需wait graphics prologueDone
    bool pass_has_frame_start_acquire(RDGPassNodeRef pass) const;

    // 收集pass的帧内跨族acquire守卫（{配对release所在pass, release队列}）——Step B统一开批
    // 检查与QFO配对序断言用。帧首acquire（source_pass==nullptr）的配对release在graphics
    // prologue批（恒为计划首，提交序天然覆盖），不产生守卫
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
    const CrossQueueSyncAnalysis& sync_analysis_;
    const BarrierGenerationPhase& barrier_generation_phase_;
    const PassBindingPhase& binding_phase_;
    RDGCrossFrameRegistry& cross_frame_registry_;   // 跨帧持久状态（RenderSystem拥有，注入；见其类注释）

    CommandRecordingResult recording_result_;

    // ============================ 帧内工作数据（on_execute期间有效；workers在其内join） ============================

    std::vector<RDGPassNodeRef> orderedPasses_;             // 经过剔除后的pass全局拓扑序，释放sweep用
    std::unordered_map<RDGPassNodeRef, uint32_t> topoIndex_;// pass在orderedPasses_中的下标位置（批次排序用）
    std::vector<QueueStream> streams_;                      // 每队列流
    std::vector<uint32_t> activeQueues_;                    // 有pass的队列下标
    std::vector<std::vector<StreamBatch>> batches_;         // 每队列的提交批次
    std::vector<std::vector<uint32_t>> planBatchIndex_;     // (q,b)→计划批次下标（提交序=拓扑序）

    std::vector<CrossQueueSyncPoint> activePoints_;         // 有效同步点（悬空已过滤）
    std::unordered_map<RDGPassNodeRef, std::vector<uint32_t>> producerPoints_;   // 生产者pass在有效同步点(activePoints_)中的下标
	std::unordered_map<RDGPassNodeRef, std::vector<uint32_t>> consumerPoints_;   // 消费者pass在有效同步点(activePoints_)中的下标
    std::unordered_map<uint32_t, uint64_t> pointValues_;    // 同步点下标→timeline值（提交序分配）

    // ---- 细链工作数据（build_submit_plan期间）----
    // (q,b)→首触键集：该批是这些imported键在队列q的首触批（跨帧wait挂点）
    std::map<std::pair<uint32_t, uint32_t>, std::vector<const void*>> firstTouchKeys_;
    // 键→队列→末触(q,b)（本帧内每键每队列的最后触批——signal与回写挂点）
    std::unordered_map<const void*, std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>>> lastUseBatch_;
    // 键→归巢载体批(q,b)：Phase 7归巢release（target_pass==nullptr的跨族release，imported与
    // 池化纹理皆有）实际挂载的批——锚定屏障真实位置而非拓扑序推断（Phase 7遍历序与本相
    // 拓扑序对不可比较pass可不一致）。prologue承接wait的数据源
    std::unordered_map<const void*, std::pair<uint32_t, uint32_t>> homingBatch_;
    std::set<std::pair<uint32_t, uint32_t>> lastUseBatches_;            // 末触批集合（值分配查询）
    std::map<std::pair<uint32_t, uint32_t>, uint64_t> batchLastUseValue_;   // 末触批→timeline值
};
