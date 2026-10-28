#include "pass_execution_phase.h"

#include "Function/Global/Definations.h"
#include "Function/Render/RHI/VulkanRHI/VulkanUtil.h"    // 取证：ResourceCast取队列句柄
#include "Function/Global/EngineContext.h"
#include "Function/Global/EngineThreadPool.h"
#include "Function/Render/RDG/RDGPool.h"

#include <algorithm>
#include <map>

// 跨帧队列链：imported持久资源（TLAS storage、阴影图、DDGI探针等）跨帧复用同一VkImage，
// 旧单队列按提交序串行天然安全；多队列下两帧GPU并发，帧N+1的写入会追上帧N的读取（WAR竞态，
// 如阴影闪烁）。本帧各队列首批次wait上一帧其他队列的queueDone，重建帧间串行语义。
// 帧内跨队列重叠不受影响。数据源=注入的跨帧注册表（RenderSystem拥有、两帧槽编译器共享，
// 语义与原文件级static一致——见cross_frame_registry.h的代际协议注释）

// 诊断日志：全局帧序号（仅enable_debug_output时递增——跨两个帧槽编译器实例关联日志用，
// 主线程逐帧串行访问；worker录制期的[Rec]日志只读它，写入发生在派发之前）
static uint32_t s_debugFrameSeq = 0;

PassExecutionPhase::PassExecutionPhase(
    const QueueSchedule& queue_schedule,
    const ExecutionReorderPhase& reorder_phase,
    const BarrierGenerationPhase& barrier_generation_phase,
    const PassBindingPhase& binding_phase,
    RDGCrossFrameRegistry& cross_frame_registry,
    const CommandRecordingConfig& config)
    : config_(config)
    , queue_schedule_(queue_schedule)
    , reorder_phase_(reorder_phase)
    , barrier_generation_phase_(barrier_generation_phase)
    , binding_phase_(binding_phase)
    , cross_frame_registry_(cross_frame_registry)
{
}

void PassExecutionPhase::reset_for_frame()
{
    recording_result_.total_passes_executed.store(0, std::memory_order_relaxed);
    recording_result_.total_barriers_inserted.store(0, std::memory_order_relaxed);
    recording_result_.total_submit_batches = 0;

    orderedPasses_.clear();
    topoIndex_.clear();
    streams_.clear();
    planBatchIndex_.clear();
    activeQueues_.clear();
    batches_.clear();
}

void PassExecutionPhase::on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{
    ENGINE_TIME_SCOPE(PassExecutionPhase::on_execute);

    const uint32_t debugFrame = config_.enable_debug_output ? ++s_debugFrameSeq : 0;
    if (config_.enable_debug_output)
        ENGINE_LOG_INFO("[Phase8] ===== frame #{} begin =====", debugFrame);

    // Step A：队列流构建（含串行边界与有效同步点过滤）
    build_queue_streams(graph, executor);
    if (activeQueues_.empty()) return;

    // Step B：值级守卫的提交批次切分（覆盖串行区）
    split_submit_batches(executor);

    // Step C：提交计划组装（零值分配——wait/signal全由Phase 7定稿）
    build_submit_plan(executor);

    // Step D：piece均衡分配 + 并行录制 + 串行区主线程录制 + 回填计划
    record_all(graph, executor);

    // 释放sweep：录制期不碰任何池（RDG池无锁，不能并发）——全部录制完成后按拓扑全序统一执行last-use释放
    release_sweep(graph, orderedPasses_);

    if (config_.enable_debug_output)
    {
        // 安全网：帧结束时仍持有物理资源且未释放的非imported节点 = 泄漏源——
        // 该资源的池条目永远不会归还，下一帧必然新建一张，直到设备内存超限。
        // 正常情况（分配已被生命期过滤、last-use释放完整）此处不应有任何输出
        graph->ForEachTextureNode([&](RDGTextureNodeRef textureNode) {
            if (!textureNode->IsImported() && textureNode->texture != nullptr)
                ENGINE_LOG_WARN("[PassExecution] leaked texture '{}' ({}x{}, mip={}) never released",
                    textureNode->Name().c_str(),
                    textureNode->info.extent.width, textureNode->info.extent.height,
                    textureNode->info.mipLevels);
            });
        graph->ForEachBufferNode([&](RDGBufferNodeRef bufferNode) {
            if (!bufferNode->IsImported() && bufferNode->buffer != nullptr)
                ENGINE_LOG_WARN("[PassExecution] leaked buffer '{}' (size={}) never released",
                    bufferNode->Name().c_str(), bufferNode->info.size);
            });
    }

    // 尾声：池化描述符集必须等全部pass录制完再归还
    release_pooled_descriptor_sets(graph);

    if (config_.enable_debug_output)
    {
        ENGINE_LOG_INFO("[Phase8] passes: {}, barriers: {}, submit batches: {}",
            recording_result_.total_passes_executed.load(std::memory_order_relaxed),
            recording_result_.total_barriers_inserted.load(std::memory_order_relaxed),
            recording_result_.total_submit_batches);
        ENGINE_LOG_INFO("[Phase8] ===== frame #{} end =====", debugFrame);
    }
}

// Step A ////////////////////////////////////////////////////////////////////////

void PassExecutionPhase::build_queue_streams(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{
    // 规范全局序（剔除culled）——批次排序键与串行边界投影的基准。
    // 主路径=Phase 3的HEFT全局调度序（流构造序=调度序的队列投影）。排序键与流构造序必须
    // 一致（否则stable_sort打乱同队列批次、破坏timeline signal单调性→GPU死锁）。
    // 空图/零队列早退时schedule_order为空⇒orderedPasses_为空（原级别序兜底已随拓扑产出瘦身删除）

    // 将全局序压入orderedPasses_，并记录其在全局序中的下标（topoIndex_）
    // 用于Step C 的所有队列的提交批次排序 和 串行边界投影
    const TimelineScheduleResult& scheduleForOrder = queue_schedule_.get_schedule_result();
    for (RDGPassNodeRef pass : scheduleForOrder.schedule_order)
    {
        if (pass && !pass->isCulled)
        {
            topoIndex_[pass] = static_cast<uint32_t>(orderedPasses_.size());
            orderedPasses_.push_back(pass);
        }
    }

    // 找到在全局拓扑序中的串行边界：首个命中 serial_pass_names 的位置，其后全部pass归主线程录制
    uint32_t serialTopo = static_cast<uint32_t>(orderedPasses_.size());
    if (!config_.serial_pass_names.empty())
    {
        for (uint32_t i = 0; i < orderedPasses_.size(); i++)
        {
            for (const auto& name : config_.serial_pass_names)
            {
                if (orderedPasses_[i]->Name() == name) { serialTopo = i; break; }
            }
            if (serialTopo != orderedPasses_.size()) break;
        }
    }

    // 队列流：Phase 3 按依赖级别序分配（queue_schedules内保持拓扑序），此处仅剔除culled
    const TimelineScheduleResult& schedule = queue_schedule_.get_schedule_result();

    // 对齐断言：帧槽slot由RenderSystem按QueryConfiguredQueues预建，下标须与all_queues一一对应
    assert(executor->queueSlots.size() == schedule.all_queues.size());
    for (uint32_t q = 0; q < schedule.all_queues.size(); q++)
        assert(executor->queueSlots[q].queue == schedule.all_queues[q].handle);

    // 一个队列一个流
    streams_.assign(schedule.all_queues.size(), QueueStream{});
    for (uint32_t q = 0; q < schedule.all_queues.size(); q++)
    {
        if (q < schedule.queue_schedules.size())
        {
            // 按照Phase 3 的规划将每一个pass分配到对应的队列流中，剔除被cull的pass
            for (RDGPassNodeRef pass : schedule.queue_schedules[q])
                if (pass && !pass->isCulled) streams_[q].passes.push_back(pass);
        }
    }

    // 各队列流内的串行区起点
    for (uint32_t q = 0; q < streams_.size(); q++)
    {
        QueueStream& stream = streams_[q];
        stream.serialPos = static_cast<uint32_t>(stream.passes.size());
        for (uint32_t i = 0; i < stream.passes.size(); i++)
        {
            // 流内首个 topoIndex >= serialTopo 的下标
            auto found = topoIndex_.find(stream.passes[i]);
            if (found != topoIndex_.end() && found->second >= serialTopo) { stream.serialPos = i; break; }
        }
    }

    activeQueues_.clear();
    for (uint32_t q = 0; q < streams_.size(); q++)
        // 刀2b：有pass，或为某悬空起点的挂靠队列（owner队列本帧无pass时也须激活——QFO release
        // 必须发在owner族的队列上，Step B会给它一条空prologue批）
        if (!streams_[q].passes.empty() || queue_has_dangling(q, executor))
            activeQueues_.push_back(q);

    if (config_.enable_debug_output)
    {
        ENGINE_LOG_INFO("[StepA] queues: total={} active={} | orderedPasses={}",
            streams_.size(), activeQueues_.size(), orderedPasses_.size());
        for (uint32_t q : activeQueues_)
        {
            const QueueStream& stream = streams_[q];
            const bool hasSerial = stream.serialPos < stream.passes.size();
            ENGINE_LOG_INFO("[StepA] q{} stream: {} passes, serialPos={} ('{}')",
                q, stream.passes.size(),
                static_cast<int>(hasSerial ? stream.serialPos : -1),
                hasSerial ? stream.passes[stream.serialPos]->Name().c_str() : "-");
        }
    }
}

// Step B ////////////////////////////////////////////////////////////////////////

void PassExecutionPhase::split_submit_batches(PerFrameCommonResourceRef executor)
{
    batches_.assign(streams_.size(), {});

    for (uint32_t q : activeQueues_)
    {
        const QueueStream& stream = streams_[q];
        std::vector<StreamBatch>& queueBatches = batches_[q];

        // 对照实验：回滚空批机制——悬空release回到与b0合批形态（(a)验收绿版结构）。
        // 空流+悬空：仍需空批（QFO release须发在owner族队列上，无pass时唯一载体）
        if (stream.passes.empty())
        {
            if (queue_has_dangling(q, executor))
            {
                StreamBatch prologueOnly;
                prologueOnly.begin = prologueOnly.end = 0;
                prologueOnly.serial = true;
                prologueOnly.isDanglingPrologue = true;
                queueBatches.push_back(prologueOnly);
            }
            continue;
        }

        // 刀C防御补全：流首pass含帧首跨族acquire且本队列有悬空——先垫一条空prologue批承载
        // 悬空release。主循环的guard-acquire-dangling守卫只对i>0生效（i==0无法"切批前移"），
        // 不垫批则该acquire落在sortKey=0段、与对方prologue批互序无定义（审计②当场断言）
        if (queue_has_dangling(q, executor))
        {
            std::vector<std::pair<RDGPassNodeRef, uint32_t>> firstGuards;
            collect_in_frame_acquire_guards(stream.passes[0], firstGuards);
            for (const auto& guard : firstGuards)
                if (topoIndex_.find(guard.first) == topoIndex_.end())   // 帧首acquire哨兵
                {
                    StreamBatch prologueOnly;
                    prologueOnly.begin = prologueOnly.end = 0;
                    prologueOnly.serial = true;
                    prologueOnly.isDanglingPrologue = true;
                    queueBatches.push_back(prologueOnly);
                    break;
                }
        }

        // 值级守卫（时间线双signal模型）：本队列已等待的各时间线最大值——pass的结构体A waits
        // 已在Phase 7分配具体值，支配判定直接值级（旧位置级coveredMaxPos随点体系退役）。
        //   任一wait(T,v)满足v>waitedMax[T]则开批发wait：跨队列的提交序不蕴含执行序，
        //   等不过=读到未写完的数据。本队列时间线同样适用此判据——批头已等过的own值其signal
        //   必在更早批（值≤已等值<本批任何signal，B单调），不会命中规则③的同批自等
        std::unordered_map<const void*, uint64_t> waitedMax;
        auto foldNodeWaits = [&waitedMax](const RDGSyncNode* node)
        {
            if (node == nullptr) return;
            for (const auto& [timeline, value] : node->waits)
            {
                uint64_t& slot = waitedMax[timeline.get()];
                slot = std::max(slot, value);
            }
        };

        StreamBatch current;
        current.begin = 0;
        bool currentOpen = false;
        uint32_t headTopo = 0;                          // 当前批首pass的canonical位置
        const char* pendingOpenReason = "stream-start"; // 下一批的开批原因（日志用）

        for (uint32_t i = 0; i < stream.passes.size(); i++)
        {
            RDGPassNodeRef pass = stream.passes[i];
            const RDGSyncNode* node = barrier_generation_phase_.get_pass_sync_node(pass);

            if (currentOpen)
            {
                bool mustOpen = false;
                const char* reason = nullptr;

                if (i == stream.serialPos)
                {
                    // 串行边界强制切批（串行区批次归主线程join后录制，批内不能混跨边界）
                    mustOpen = true;
                    reason = "serial-boundary";
                }
                else
                {
                    // 值级wait守卫：未被支配覆盖的wait必须开批发wait（跨队列的提交序不蕴含
                    // 执行序，等不过=读到未写完的数据）
                    if (node != nullptr)
                    {
                        for (const auto& [timeline, value] : node->waits)
                        {
                            auto it = waitedMax.find(timeline.get());
                            if (it == waitedMax.end() || value > it->second)
                            {
                                mustOpen = true;
                                reason = (timeline == executor->queueSlots[q].timeline) ? "guard-own-timeline" : "guard-wait";
                                break;
                            }
                        }
                    }

                    // 帧内acquire守卫（QFO配对序）：只能由提交序覆盖——合批后批头topo可早于
                    // 配对release位置（值级wait守卫不保证覆盖它：acquire者等的是生产者的数据
                    // 就绪，配对release挂生产者pass批尾，两者同批也满足数据序但提交序失效）。
                    // 批头topo≤release位置则开批，使release批键<acquire批键
                    if (!mustOpen)
                    {
                        std::vector<std::pair<RDGPassNodeRef, uint32_t>> acquireGuards;
                        collect_in_frame_acquire_guards(pass, acquireGuards);
                        for (const auto& guard : acquireGuards)
                        {
                            auto posIt = topoIndex_.find(guard.first);
                            if (posIt == topoIndex_.end())
                            {
                                // 帧首acquire哨兵：配对release在owner队列的prologue批（sortKey=0段）。
                                // 若本批也是prologue（b0且本队列有悬空→sortKey=0），prologue批间提交序
                                // 无定义→QFO配对序可能倒挂（当年2434条no-matching的可疑成因）——切批
                                if (current.begin == 0 && queue_has_dangling(q, executor))
                                {
                                    mustOpen = true;
                                    reason = "guard-acquire-dangling";
                                    break;
                                }
                                continue;
                            }
                            if (headTopo <= posIt->second)
                            {
                                mustOpen = true;
                                reason = "guard-acquire";
                                break;
                            }
                        }
                    }
                }

                if (mustOpen)
                {
                    current.end = i;
                    queueBatches.push_back(current);
                    currentOpen = false;
                    pendingOpenReason = reason;
                }
            }

            if (!currentOpen)
            {
                current = StreamBatch{};
                current.begin = i;
                current.openReason = pendingOpenReason;
                currentOpen = true;
                pendingOpenReason = "stream-end";

                auto headIt = topoIndex_.find(pass);
                assert(headIt != topoIndex_.end());
                headTopo = headIt->second;

                // 批头pass的waits折入支配表（Step C组装做同构值级去重）
                foldNodeWaits(node);
            }

            current.hasPresent = current.hasPresent || pass->NodeType() == RDG_PASS_NODE_TYPE_PRESENT;
            // producer收批规则删除：signal已按"每批每时间线合并取前缀max"发射（Step C），
            // producer不必单独收口——批尾合并值天然覆盖所有等待者
        }

        if (currentOpen)
        {
            current.end = static_cast<uint32_t>(stream.passes.size());
            queueBatches.push_back(current);
        }

        // 串行区标记（空prologue批的serial保留）
        for (auto& batch : queueBatches)
            batch.serial = batch.serial ||
                (stream.serialPos < stream.passes.size() && batch.begin >= stream.serialPos);
    }

    // QFO配对序契约（审计②，统一开关ENABLE_RDG_AUDIT）：每个帧内acquire所在批的批头canonical
    // > 配对release所在pass的canonical ⇒ release批键<acquire批键——验证层"release先于acquire提交"
    // 成立。拓扑排序构造保证（生产者批头≤生产者topo<消费者topo≤消费者批头，同队列流序=拓扑序、
    // 跨队列按批头topo稳定排序）；帧首acquire（哨兵nullptr）的配对release在owner队列prologue批
    //（sortKey=0）——断言其所在批非prologue（StepB的guard-acquire-dangling切批是其唯一防线）
#if ENABLE_RDG_AUDIT
    for (uint32_t q : activeQueues_)
        for (uint32_t b = 0; b < batches_[q].size(); b++)
        {
            const StreamBatch& batch = batches_[q][b];
            if (batch.begin >= batch.end) continue;   // 空prologue批无pass
            const uint32_t headTopo = topoIndex_[streams_[q].passes[batch.begin]];
            for (uint32_t i = batch.begin; i < batch.end; i++)
            {
                std::vector<std::pair<RDGPassNodeRef, uint32_t>> guards;
                collect_in_frame_acquire_guards(streams_[q].passes[i], guards);
                for (const auto& guard : guards)
                {
                    auto posIt = topoIndex_.find(guard.first);
                    if (posIt == topoIndex_.end())
                    {
                        assert(!(b == 0 && queue_has_dangling(q, executor)));   // 帧首acquire不得在prologue批
                        continue;
                    }
                    assert(headTopo > posIt->second);
                }
            }
        }
#endif

    if (config_.enable_debug_output)
    {
        for (uint32_t q : activeQueues_)
        {
            const QueueStream& stream = streams_[q];
            ENGINE_LOG_INFO("[StepB] q{} batches: {}", q, batches_[q].size());
            for (uint32_t b = 0; b < batches_[q].size(); b++)
            {
                const StreamBatch& batch = batches_[q][b];
                std::string range = batch.begin < batch.end
                    ? "'" + std::string(stream.passes[batch.begin]->Name()) + "'..'" + stream.passes[batch.end - 1]->Name() + "'"
                    : "(prologue-only)";
                ENGINE_LOG_INFO("[StepB] q{} batch[{}] passes[{}..{}] {} open={}{}{}",
                    q, b, batch.begin, batch.end - 1, range,
                    batch.openReason,
                    batch.serial ? " [serial]" : "", batch.hasPresent ? " [present]" : "");
            }
        }
    }
}

// Step C ////////////////////////////////////////////////////////////////////////
void PassExecutionPhase::build_submit_plan(PerFrameCommonResourceRef executor)
{
    RHIQueueSubmitPlan& plan = executor->submitPlan;
    plan.batches.clear();

    const TimelineScheduleResult& schedule = queue_schedule_.get_schedule_result();

    // graphics队列下标（Present/swapchain所在；present恒为graphics分类）
    uint32_t graphicsQueue = activeQueues_.front();
    for (uint32_t q : activeQueues_)
        if (schedule.all_queues[q].type == ERenderGraphQueueType::Graphics) { graphicsQueue = q; break; }


    // ---- 组装（零值分配：wait/signal全由Phase 7的结构体A/B定稿，本阶段只做聚合）----
    struct PlanEntry
    {
        uint32_t sortKey;               // 批次首个pass的拓扑序（悬空载体批强制0）
        uint32_t queue;
        uint32_t batch;
        RHIQueueSubmitBatch submit;
    };
    std::vector<PlanEntry> entries;
    std::vector<uint64_t> queueLastSignal(streams_.size(), 0);   // 各队列最后实发值（join批wait用）

    for (uint32_t q : activeQueues_)
    {
        // 值级支配去重表（与Step B同构——Step B已保证批内wait全被批头覆盖，此处防重复wait条目）：
        // ≤已等值的wait被本队列提交序串行蕴含（时间线值单调，等过大值蕴含小值已满足）——不发
        std::unordered_map<RHISemaphoreRef, uint64_t> waitedMax;
        // 同时间线已发signal的运行最大值：批signal=前缀max。悬空起点的signal分配于走查中段、
        // 可能大于owner队列早段pass的分配值——批间取前缀max并只发"有新增"的批，保证同时间线
        // signal按提交序严格递增且无重复（规则①）；批内小值不实发，等待它们被更大值蕴含满足
        std::unordered_map<RHISemaphoreRef, uint64_t> runningSignalMax;

        for (uint32_t b = 0; b < batches_[q].size(); b++)
        {
            const StreamBatch& batch = batches_[q][b];

            RHIQueueSubmitBatch submitBatch;
            submitBatch.queue = executor->queueSlots[q].queue;

            // 聚合：批内全体pass（b==0时含悬空起点）——waits逐时间线max / signal逐时间线max
            std::unordered_map<RHISemaphoreRef, uint64_t> batchWaits;
            std::unordered_map<RHISemaphoreRef, uint64_t> batchSignals;
            auto foldNode = [&](const RDGSyncNode& node)
            {
                if (node.signal.first != nullptr)
                {
                    uint64_t& s = batchSignals[node.signal.first];
                    s = std::max(s, node.signal.second);
                }
                for (const auto& [timeline, value] : node.waits)
                {
                    uint64_t& w = batchWaits[timeline];
                    w = std::max(w, value);
                }
            };
            for (uint32_t i = batch.begin; i < batch.end; i++)
                if (const RDGSyncNode* node = barrier_generation_phase_.get_pass_sync_node(streams_[q].passes[i]))
                    foldNode(*node);
            if (b == 0 && queue_has_dangling(q, executor))    // 悬空并入b0（合批形态）
                if (const RDGSyncNode* dangling = queue_dangling_node(q, executor))
                    foldNode(*dangling);

#if ENABLE_RDG_AUDIT
            // 刀B：吞批禁令断言——批内含本队列时间线的signal工作（有pass/悬空要公示完成）但
            // own值≤此前已发射值 = 值序与提交序倒挂（前批发射值越过本批工作的值，等待者会被
            // 提前放行——syncval 980条hazard案的病根形态，2026-10-28）。构造上已被悬空signal
            // 低位预分配（prebuild_dangling_starts）杜绝，此断言为该不变量的永久防线
            {
                const RHISemaphoreRef& ownTl = executor->queueSlots[q].timeline;
                auto ownIt = batchSignals.find(ownTl);
                if (ownIt != batchSignals.end())
                {
                    uint64_t prevEmitted = 0;
                    if (auto r = runningSignalMax.find(ownTl); r != runningSignalMax.end())
                        prevEmitted = r->second;
                    assert(ownIt->second > prevEmitted);   // 本批工作值必须大于此前一切发射值
                }
            }
#endif

            // waits：值级支配去重后发射。宽掩码：wait聚合自多资源多用途，stage无从收窄（窄化是性能债）
            for (const auto& [timeline, value] : batchWaits)
            {
                uint64_t& maxWaited = waitedMax[timeline];
                if (value <= maxWaited) continue;
                maxWaited = value;
                RHISemaphoreWaitInfo wait{};
                wait.semaphore = timeline;
                wait.value = value;
                wait.isTimeline = true;
                wait.waitState = RESOURCE_STATE_UNDEFINED;
                submitBatch.waits.push_back(wait);
            }

            // graphics首批次：swapchain acquire（二进制；宽掩码行为见VulkanRHICommandContext::Submit的UNDEFINED约定）
            if (q == graphicsQueue && b == 0)
            {
                RHISemaphoreWaitInfo wait{};
                wait.semaphore = executor->startSemaphore;
                wait.isTimeline = false;
                wait.waitState = RESOURCE_STATE_UNDEFINED;     // UNDEFINED=保持旧宽掩码（COLOR|COMPUTE|TRANSFER）
                submitBatch.waits.push_back(wait);
            }

            // signals：每时间线至多一条（规则①同批同线只能signal一次）=运行前缀max，仅当本批
            // 贡献了更大值时发射。present批：finishSemaphore（present等待它）
            for (const auto& [timeline, value] : batchSignals)
            {
                uint64_t& running = runningSignalMax[timeline];
                if (value <= running) continue;    // 本批无新增：已被更早批的signal蕴含
                running = value;
                RHISemaphoreSubmitInfo signal{};
                signal.semaphore = timeline;
                signal.value = running;
                signal.isTimeline = true;
                submitBatch.signals.push_back(signal);
                if (timeline == executor->queueSlots[q].timeline)
                    queueLastSignal[q] = running;
            }
            if (batch.hasPresent)
            {
                RHISemaphoreSubmitInfo signal{};
                signal.semaphore = executor->finishSemaphore;
                signal.isTimeline = false;
                submitBatch.signals.push_back(signal);
            }

            PlanEntry entry;
            // 排序键：悬空载体批（b0=本队列首批pass+悬空fold，合批形态）=0（占0段；批间互序由
            // StepB的guard-acquire-dangling切批保证安全——含帧首acquire的pass已被切出0段）；
            // 其余批=批头规范序+1（偏移1保证一切含acquire的批严格后于【全部】prologue批提交，
            // QFO要求release先提交。普通批间相对序不变；帧内配对序由Step B的acquire守卫保证）
            if (b == 0 && queue_has_dangling(q, executor))
                entry.sortKey = 0;
            else
                entry.sortKey = topoIndex_[streams_[q].passes[batch.begin]] + 1;
            entry.queue = q;
            entry.batch = b;
            entry.submit = std::move(submitBatch);
            entries.push_back(std::move(entry));
        }
    }

    // 稳定排序（拓扑序）后落盘计划，并记录 (q,b)→计划下标映射（Step D回填用）
    std::stable_sort(entries.begin(), entries.end(),
        [](const PlanEntry& a, const PlanEntry& b) { return a.sortKey < b.sortKey; });

    planBatchIndex_.assign(streams_.size(), {});
    for (uint32_t q : activeQueues_) planBatchIndex_[q].assign(batches_[q].size(), 0);
    std::vector<std::pair<uint32_t, uint32_t>> planQB;   // 取证：plan序→(q,b)，[StepC] dump与[StepB]对齐用
    for (uint32_t i = 0; i < entries.size(); i++)
    {
        planBatchIndex_[entries[i].queue][entries[i].batch] = static_cast<uint32_t>(plan.batches.size());
        planQB.emplace_back(entries[i].queue, entries[i].batch);
        plan.batches.push_back(std::move(entries[i].submit));
    }

    // join批：空提交（无命令缓冲），等待各队列时间线的最后实发signal（天然queueDone）→
    // signal帧槽fence。单队列时省略——fence直接挂该队列末批（单队列提交序串行，末批完成⇒全帧完成）
    if (activeQueues_.size() > 1)
    {
        RHIQueueSubmitBatch join;
        join.queue = executor->queueSlots[graphicsQueue].queue;
        for (uint32_t q : activeQueues_)
        {
            RHISemaphoreWaitInfo wait{};
            wait.semaphore = executor->queueSlots[q].timeline;
            wait.value = queueLastSignal[q];
            wait.isTimeline = true;
            wait.waitState = RESOURCE_STATE_SHADER_RESOURCE;    // 纯中继批，stage无实际意义
            join.waits.push_back(wait);
        }
        join.signalFence = executor->fence;
        plan.batches.push_back(std::move(join));
    }
    else if (!plan.batches.empty())
    {
        plan.batches.back().signalFence = executor->fence;
    }

    recording_result_.total_submit_batches = static_cast<uint32_t>(plan.batches.size());

    // registry换代：import资源的同步记录（pendingSyncRecords_）由Phase 7走查期间写入，此处原子
    // 换代（merge语义见cross_frame_registry——未触键保留旧记录，族信息跨帧有效）
    cross_frame_registry_.commit_frame();

    // 计划自检（审计⑤，ENABLE_RDG_AUDIT）——构造性不变量：startSemaphore恰一wait、
    // finishSemaphore恰一signal、同一时间线的signal值按提交序严格递增且无重复、
    // 同一批次内同时间线的signal值大于wait值（Vulkan单提交内约束）、每批每时间线至多一条signal
#if ENABLE_RDG_AUDIT
    {
        uint32_t startWaits = 0, finishSignals = 0;
        std::unordered_map<const void*, uint64_t> lastSignalValuePerTimeline;          // timeline→最近signal值（提交序）
        std::map<std::pair<const void*, uint64_t>, uint32_t> timelineSignalCounts;     // (timeline,value)→次数
        for (const RHIQueueSubmitBatch& batch : plan.batches)
        {
            std::unordered_map<const void*, uint64_t> waitValuePerTimeline;
            std::unordered_map<const void*, uint32_t> signalCountPerTimeline;
            for (const auto& wait : batch.waits)
            {
                if (!wait.isTimeline && wait.semaphore == executor->startSemaphore) startWaits++;
                if (wait.isTimeline)
                    waitValuePerTimeline[wait.semaphore.get()] = std::max(waitValuePerTimeline[wait.semaphore.get()], wait.value);
            }
            for (const auto& signal : batch.signals)
            {
                if (!signal.isTimeline && signal.semaphore == executor->finishSemaphore) finishSignals++;
                if (signal.isTimeline)
                {
                    const void* timeline = signal.semaphore.get();
                    auto last = lastSignalValuePerTimeline.find(timeline);
                    if (last != lastSignalValuePerTimeline.end())
                        assert(signal.value > last->second);    // 同一时间线按提交序严格递增
                    auto waitBound = waitValuePerTimeline.find(timeline);
                    if (waitBound != waitValuePerTimeline.end())
                        assert(signal.value > waitBound->second);   // 单提交内signal>wait
                    lastSignalValuePerTimeline[timeline] = signal.value;
                    timelineSignalCounts[{timeline, signal.value}]++;
                    signalCountPerTimeline[timeline]++;
                }
            }
            for (const auto& [timeline, count] : signalCountPerTimeline)
                assert(count <= 1);    // 每批每时间线至多一条（规则①）
        }
        assert(startWaits == 1);       // Present恒存在→graphics首批次恒存在
        assert(finishSignals == 1);
        for (const auto& [key, count] : timelineSignalCounts) assert(count == 1);
    }
#endif

    if (config_.enable_debug_output)
    {
        // 按提交序dump整帧计划（每行=一次vkQueueSubmit2，含末尾join批）。waitState决定后端推导
        // 的stage掩码：WIDE=UNDEFINED宽掩码（当前恒宽——聚合wait无从收窄）
        auto stateTag = [](RHIResourceState state) -> std::string
        {
            return state == RESOURCE_STATE_UNDEFINED ? "WIDE" : std::to_string(static_cast<int>(state));
        };

        uint32_t totalWaits = 0, totalSignals = 0, totalFences = 0;
        for (uint32_t i = 0; i < plan.batches.size(); i++)
        {
            const RHIQueueSubmitBatch& batch = plan.batches[i];

            // 反查批次所属队列（join批空命令也可能在graphics上）
            uint32_t q = 0;
            for (uint32_t s = 0; s < streams_.size(); s++)
                if (executor->queueSlots[s].queue == batch.queue) { q = s; break; }

            std::string waits;
            for (const auto& wait : batch.waits)
            {
                totalWaits++;
                if (!wait.isTimeline && wait.semaphore == executor->startSemaphore)
                {
                    waits += "acquire(bin,st" + stateTag(wait.waitState) + ") ";
                    continue;
                }
                if (wait.isTimeline)
                {
                    // 跨帧wait引用上帧槽的时间线，本帧反查不到——xf标注防误导（值属上帧的timeline）
                    uint32_t srcQ = 0; bool found = false;
                    for (uint32_t s = 0; s < streams_.size(); s++)
                        if (executor->queueSlots[s].timeline == wait.semaphore) { srcQ = s; found = true; break; }
                    waits += (found ? "q" + std::to_string(srcQ) : "xf")
                        + "(v" + std::to_string(wait.value) + ",st" + stateTag(wait.waitState) + ") ";
                }
            }
            std::string signals;
            for (const auto& signal : batch.signals)
            {
                totalSignals++;
                if (!signal.isTimeline && signal.semaphore == executor->finishSemaphore)
                {
                    signals += "presentFinish(bin) ";
                    continue;
                }
                if (signal.isTimeline)
                    signals += "q" + std::to_string(q) + "tl(v" + std::to_string(signal.value) + ") ";
            }
            if (batch.signalFence != nullptr) totalFences++;

            ENGINE_LOG_INFO("[StepC] plan[{}] q{}b{} waits[{}]: {}| signals[{}]: {}| fence={}",
                i, q, i < planQB.size() ? planQB[i].second : 0, batch.waits.size(), waits, batch.signals.size(), signals,
                batch.signalFence != nullptr ? "frameSlot" : "-");
        }
        ENGINE_LOG_INFO("[StepC] plan total: {} submits, {} waits, {} signals, {} fences",
            plan.batches.size(), totalWaits, totalSignals, totalFences);
    }
}

// Step D ////////////////////////////////////////////////////////////////////////

void PassExecutionPhase::record_all(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{
    const uint32_t workerBudget = config_.chunk_count > 0 ? config_.chunk_count - 1 : 0;

    // 1) 预串行批次的piece切分：跨全队列按pass数动态均衡。
    //    piece不跨批次（批次是vkQueueSubmit边界，跨批次拼接会破坏信号量时序）
    std::vector<RecordPiece> pieces;
    uint32_t totalPreserial = 0;
    for (uint32_t q : activeQueues_)
        for (const StreamBatch& batch : batches_[q])
            if (!batch.serial) totalPreserial += batch.end - batch.begin;

    const uint32_t target = workerBudget > 0 && totalPreserial > 0
        ? (totalPreserial + workerBudget - 1) / workerBudget : totalPreserial;

    for (uint32_t q : activeQueues_)
    {
        for (uint32_t b = 0; b < batches_[q].size(); b++)
        {
            const StreamBatch& batch = batches_[q][b];
            if (batch.serial) continue;
            for (uint32_t s = batch.begin; s < batch.end; s += target)
            {
                RecordPiece piece{};
                piece.queue = q;
                piece.batchIndex = b;
                piece.begin = s;
                piece.end = std::min(s + target, batch.end);
                pieces.push_back(piece);
            }
        }
    }

    // piece数不足预算时继续二分最大piece填满worker（全为单pass则停止）。
    // 二分产生的两个半段保持相邻（同批次内顺序不变），回填按batchIndex分桶时序仍正确
    while (pieces.size() < workerBudget)
    {
        auto largest = std::max_element(pieces.begin(), pieces.end(),
            [](const RecordPiece& a, const RecordPiece& b) { return (a.end - a.begin) < (b.end - b.begin); });
        if (largest == pieces.end() || largest->end - largest->begin < 2) break;

        RecordPiece second = *largest;
        const uint32_t mid = (second.begin + second.end) / 2;
        largest->end = mid;
        second.begin = mid;
        pieces.push_back(second);
    }

    if (config_.enable_debug_output)
    {
        uint32_t serialBatches = 0;
        for (uint32_t q : activeQueues_)
            for (const StreamBatch& batch : batches_[q]) if (batch.serial) serialBatches++;
        ENGINE_LOG_INFO("[StepD] pieces: {} (workerBudget={}, target={} passes/piece), serial batches: {}",
            pieces.size(), workerBudget, target, serialBatches);
        for (uint32_t p = 0; p < pieces.size(); p++)
            ENGINE_LOG_INFO("[StepD] piece[{}] q{} batch[{}] passes[{}..{}] '{}'..'{}'",
                p, pieces[p].queue, pieces[p].batchIndex, pieces[p].begin, pieces[p].end - 1,
                streams_[pieces[p].queue].passes[pieces[p].begin]->Name().c_str(),
                streams_[pieces[p].queue].passes[pieces[p].end - 1]->Name().c_str());
    }

    // 2) 命令流分配：每队列按序给piece/串行批次各编一条命令流（一条流独占一个context，
    //    每帧BeginCommand重置一次）。先按需求量增长（piece数+串行批次数可能超过预建数），
    //    再按下标取用——顺序不能反，否则越界
    std::vector<uint32_t> nextListIndex(streams_.size(), 0);
    std::vector<RHICommandListRef> pieceCommands(pieces.size());
    std::vector<std::vector<RHICommandListRef>> serialCommands(streams_.size());

    for (uint32_t q : activeQueues_)
    {
        uint32_t required = 0;
        for (uint32_t p = 0; p < pieces.size(); p++)
            if (pieces[p].queue == q) required++;
        for (const StreamBatch& batch : batches_[q])
            if (batch.serial) required++;
        executor->queueSlots[q].EnsureCommandCount(required);
    }

    for (uint32_t p = 0; p < pieces.size(); p++)
    {
        const uint32_t listIndex = nextListIndex[pieces[p].queue]++;
        pieceCommands[p] = executor->queueSlots[pieces[p].queue].commands[listIndex];
    }
    for (uint32_t q : activeQueues_)
    {
        for (const StreamBatch& batch : batches_[q])
        {
            if (!batch.serial) continue;
            const uint32_t listIndex = nextListIndex[q]++;
            serialCommands[q].push_back(executor->queueSlots[q].commands[listIndex]);
        }
    }

    // 3) workers并行录制（AddQueuedWork的包装lambda会把派发时的帧号stamp到worker的
    //    thread_local——lambda内ThreadFrameIndex()自动正确；workers在on_execute内join，
    //    this及成员引用的生命期由join保证）。悬空release不在此路径——它只由悬空prologue
    //    空批（serial，主线程串行段）携带，worker的piece与release彻底解耦
    for (uint32_t p = 0; p < pieces.size(); p++)
    {
        RHICommandListRef command = pieceCommands[p];
        const uint32_t q = pieces[p].queue;
        const uint32_t begin = pieces[p].begin;
        const uint32_t end = pieces[p].end;
        // 对照实验：恢复piece0的悬空emit（合批形态——(a)验收绿版结构）
        // 刀C：归属改判"该队列首条批"（batchIndex==0）——空prologue垫批后首条实批begin同为0，
        // 旧begin==0判定会双发release（第二条无配对acquire）
        const bool prologuePiece = pieces[p].batchIndex == 0 && queue_has_dangling(q, executor);
        EngineContext::ThreadPool()->AddQueuedWork([this, graph, executor, q, begin, end, command, prologuePiece]()
        {
            command->BeginCommand();
            if (prologuePiece)
                if (const RDGSyncNode* dangling = queue_dangling_node(q, executor))
                {
                    // [DanglingEmit]取证日志已删（2026-10-28收尾：布局案结案）
                    for (const auto& barrierBatch : dangling->batches)
                        for (const RDGBarrier& barrier : barrierBatch.barriers)
                            emit_barrier(command, barrier);
                }
            record_stream_range(graph, executor, q, streams_[q], begin, end, command);
            command->EndCommand();
        });
    }

    // join：此刻ANY池没有其他任务，全池等待即等全部piece录制完成
    EngineContext::ThreadPool()->WaitIdle(ENGINE_THREAD_TYPE_ANY);

    // 4) 主线程录串行批次（时间上最后，与一切并发录制无重叠——Editor UI的ImGui全局态/
    //    编辑器直写setting成员不会与worker竞态）；每批次一条命令流顺序录制
    for (uint32_t q : activeQueues_)
    {
        uint32_t serialCursor = 0;
        for (uint32_t b = 0; b < batches_[q].size(); b++)   // 刀C：悬空emit归属=首条批（b==0），begin==0在垫批后会双发
        {
            StreamBatch& batch = batches_[q][b];
            if (!batch.serial) continue;
            RHICommandListRef command = serialCommands[q][serialCursor++];
            command->BeginCommand();
            // 悬空emit：合批形态（流首批+有悬空）——空流空批同样命中
            if (b == 0 && queue_has_dangling(q, executor))
                if (const RDGSyncNode* dangling = queue_dangling_node(q, executor))
                {
                    // [DanglingEmit]取证日志已删（2026-10-28收尾：布局案结案）
                    for (const auto& barrierBatch : dangling->batches)
                        for (const RDGBarrier& barrier : barrierBatch.barriers)
                            emit_barrier(command, barrier);
                }
            record_stream_range(graph, executor, q, streams_[q], batch.begin, batch.end, command);
            command->EndCommand();
            batch.commandLists = { command };
        }
    }

    // 5) 回填：piece的命令流按批序写入计划批次（数组序=执行序——一次vkQueueSubmit2内多个
    //    primary buffer按序执行，语义等价单buffer串接）；计划批次在Step C按拓扑序生成，
    //    经planBatchIndex_映射对齐搬运；join批位于计划末尾无命令缓冲，无需处理
    for (uint32_t q : activeQueues_)
    {
        for (uint32_t b = 0; b < batches_[q].size(); b++)
        {
            StreamBatch& batch = batches_[q][b];
            if (!batch.serial)
            {
                for (uint32_t p = 0; p < pieces.size(); p++)
                {
                    if (pieces[p].queue == q && pieces[p].batchIndex == b)
                        batch.commandLists.push_back(pieceCommands[p]);
                }
            }
            executor->submitPlan.batches[planBatchIndex_[q][b]].commandLists = std::move(batch.commandLists);
        }
    }

    if (config_.enable_debug_output)
    {
        // 回填后每批命令流数（=该次vkQueueSubmit2内primary buffer数；0=空提交/信号量中继）
        for (uint32_t i = 0; i < executor->submitPlan.batches.size(); i++)
            ENGINE_LOG_INFO("[StepD] plan[{}] commandLists: {}",
                i, executor->submitPlan.batches[i].commandLists.size());
    }
}


// 录制 //////////////////////////////////////////////////////////////////////////

void PassExecutionPhase::record_stream_range(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor,
                                             uint32_t queueIndex, const QueueStream& stream,
                                             uint32_t beginIndex, uint32_t endIndex, RHICommandListRef command)
{
    for (uint32_t i = beginIndex; i < endIndex; i++)
    {
        // GPU时间戳打点：CommandRecordingConfig::enable_gpu_timing（编译侧开关）与每帧传入的
        // RHIRenderQuery非空（资源侧载体，RenderSystem按info的enable位决定是否传入）共同判定。
        // 每pass两个打点（索引2i/2i+1，预算=每队列256须≥2×pass数）：
        //   pre（名+" wait"）= 前一pass结束→本pass首命令执行的间隔——含批首信号量等待与队内空转
        //     （vkQueueSubmit2的timeline wait阻塞整批命令，wait落在批首pass的pre差值里）；
        //   post（pass名）  = 本pass真实执行时长。
        // 多队列各流索引都从0起，(帧槽,队列)二维分区互不冲突；worker并行录制时下标天然确定；
        // worker的ThreadFrameIndex已被帧号stamp
        if (config_.enable_gpu_timing && executor->renderQuery != nullptr)
            executor->renderQuery->WriteTimestamp(command, EngineContext::ThreadPool()->ThreadFrameIndex(), queueIndex, 2 * i, stream.passes[i]->Name() + " wait");

        execute_pass(graph, executor, queueIndex, stream.passes[i], command);

        if (config_.enable_gpu_timing && executor->renderQuery != nullptr)
            executor->renderQuery->WriteTimestamp(command, EngineContext::ThreadPool()->ThreadFrameIndex(), queueIndex, 2 * i + 1, stream.passes[i]->Name());
    }
}

void PassExecutionPhase::execute_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor,
                                      uint32_t queueIndex, RDGPassNodeRef pass, RHICommandListRef command)
{
    ENGINE_TIME_SCOPE_STR("PassExecutionPhase::" + pass->Name());

    if (config_.enable_debug_output)
    {
        // 逐pass的屏障负载（before=pass前转移/acquire，after=pass后收敛/release）——
        // worker并发输出，spdlog线程安全
        const RDGSyncNode* node = barrier_generation_phase_.get_pass_sync_node(pass);
        uint32_t before = 0, after = 0;
        if (node)
            for (const auto& batch : node->batches)
                for (const auto& barrier : batch.barriers)
                    (barrier.after_pass ? after : before)++;
        ENGINE_LOG_INFO("[Rec] q{} '{}' barriers: before={} after={}", queueIndex, pass->Name().c_str(), before, after);
    }

    // GPU调试标记
    if (config_.enable_debug_markers)
    {
        switch (pass->NodeType())
        {
        case RDG_PASS_NODE_TYPE_RENDER:      command->PushEvent(pass->Name(), {0.0f, 0.0f, 0.0f}); break;
        case RDG_PASS_NODE_TYPE_COMPUTE:     command->PushEvent(pass->Name(), {1.0f, 0.0f, 0.0f}); break;
        case RDG_PASS_NODE_TYPE_RAY_TRACING: command->PushEvent(pass->Name(), {0.0f, 1.0f, 0.0f}); break;
        case RDG_PASS_NODE_TYPE_PRESENT:     command->PushEvent(pass->Name(), {0.0f, 0.0f, 1.0f}); break;
        case RDG_PASS_NODE_TYPE_COPY:        command->PushEvent(pass->Name(), {1.0f, 1.0f, 0.0f}); break;
        default: break;
        }
    }

    // pass之前的转移屏障（非output边的pass期间状态；跨族acquire亦在此发射）
    insert_pass_barriers(command, pass, false);

    switch (pass->NodeType())
    {
    case RDG_PASS_NODE_TYPE_RENDER:      execute_render_pass(graph, executor, queueIndex, dynamic_cast<RDGRenderPassNodeRef>(pass), command);      break;
    case RDG_PASS_NODE_TYPE_COMPUTE:     execute_compute_pass(graph, executor, queueIndex, dynamic_cast<RDGComputePassNodeRef>(pass), command);    break;
    case RDG_PASS_NODE_TYPE_RAY_TRACING: execute_ray_tracing_pass(graph, executor, queueIndex, dynamic_cast<RDGRayTracingPassNodeRef>(pass), command); break;
    case RDG_PASS_NODE_TYPE_PRESENT:     execute_present_pass(graph, executor, dynamic_cast<RDGPresentPassNodeRef>(pass), command);    break;
    case RDG_PASS_NODE_TYPE_COPY:        execute_copy_pass(graph, executor, dynamic_cast<RDGCopyPassNodeRef>(pass), command);          break;
    default:                             ENGINE_LOG_FATAL("Unsupported RDG pass type!");
    }

    // pass之后的收敛屏障（output边的产出状态；跨族release亦在此随生产者pass发射）
    insert_pass_barriers(command, pass, true);

    if (config_.enable_debug_markers)
        command->PopEvent();

    // last-use释放不在录制期做（池无锁，不能并发）——由release_sweep在全部录制完成后统一执行

    recording_result_.total_passes_executed.fetch_add(1, std::memory_order_relaxed);
}

// 按类型执行 ///////////////////////////////////////////////////////////////////

void PassExecutionPhase::execute_render_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, uint32_t queueIndex, RDGRenderPassNodeRef pass, RHICommandListRef command)
{
    const PassBindInfo * bind_info = binding_phase_.get_pass_bind_info(pass);
    assert(bind_info != nullptr);
    command->BeginRendering(bind_info->rendering_info);

    RDGPassContext context = {
        .command = command,
        .descriptors = build_descriptor_array(pass)
    };
    context.passIndex[0] = pass->passIndex[0];
    context.passIndex[1] = pass->passIndex[1];
    context.passIndex[2] = pass->passIndex[2];
    pass->execute(context);

    command->EndRendering();

}

void PassExecutionPhase::execute_compute_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, uint32_t queueIndex, RDGComputePassNodeRef pass, RHICommandListRef command)
{
    RDGPassContext context = {
        .command = command,
        .descriptors = build_descriptor_array(pass)
    };
    context.passIndex[0] = pass->passIndex[0];
    context.passIndex[1] = pass->passIndex[1];
    context.passIndex[2] = pass->passIndex[2];
    pass->execute(context);
}

void PassExecutionPhase::execute_ray_tracing_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, uint32_t queueIndex, RDGRayTracingPassNodeRef pass, RHICommandListRef command)
{
    RDGPassContext context = {
        .command = command,
        .descriptors = build_descriptor_array(pass)
    };
    context.passIndex[0] = pass->passIndex[0];
    context.passIndex[1] = pass->passIndex[1];
    context.passIndex[2] = pass->passIndex[2];
    pass->execute(context);
}

void PassExecutionPhase::execute_present_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGPresentPassNodeRef pass, RHICommandListRef command)
{
    RDGTextureNodeRef presentTexture;
    RDGTextureNodeRef texture;
    TextureSubresourceLayers subresource;

    struct TextureEdgeInfo { RDGTextureEdgeRef edge; RDGTextureNodeRef node; };
    std::vector<TextureEdgeInfo> edges;
    graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef texNode) {
        edges.push_back({ edge, texNode });
        });

    if (edges[0].edge->asPresent)
    {
        presentTexture = edges[0].node;
        texture = edges[1].node;
        subresource = edges[1].edge->subresource.aspect == TEXTURE_ASPECT_NONE ?
            binding_phase_.get_texture(texture)->GetDefaultSubresourceLayers() : edges[1].edge->subresourceLayer;
    }
    else
    {
        presentTexture = edges[1].node;
        texture = edges[0].node;
        subresource = edges[0].edge->subresource.aspect == TEXTURE_ASPECT_NONE ?
            binding_phase_.get_texture(texture)->GetDefaultSubresourceLayers() : edges[0].edge->subresourceLayer;
    }

    // swapchain图像的手工屏障循环（pass内动态屏障，通用屏障由阶段7生成）
    command->TextureBarrier({binding_phase_.get_texture(presentTexture), RESOURCE_STATE_PRESENT, RESOURCE_STATE_TRANSFER_DST});
    command->CopyTexture(  binding_phase_.get_texture(texture), subresource,
                           binding_phase_.get_texture(presentTexture), {TEXTURE_ASPECT_COLOR, 0, 0, 1});
    command->TextureBarrier({binding_phase_.get_texture(presentTexture), RESOURCE_STATE_TRANSFER_DST, RESOURCE_STATE_PRESENT});
}

void PassExecutionPhase::execute_copy_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGCopyPassNodeRef pass, RHICommandListRef command)
{
    RDGBufferNodeRef bufferFrom = nullptr;
    RDGBufferNodeRef bufferTo = nullptr;
    uint32_t offsetFrom = 0;
    uint32_t offsetTo = 0;
    uint32_t size = 0;

    RDGTextureNodeRef textureFrom = nullptr;
    RDGTextureNodeRef textureTo = nullptr;
    TextureSubresourceLayers fromSubresource;
    TextureSubresourceLayers toSubresource;

    graph->ForEachBuffer(pass, [&](RDGBufferEdgeRef edge, RDGBufferNodeRef buffer) {
        if (edge->asTransferSrc)
        {
            bufferFrom = buffer;
            offsetFrom = edge->offset;
            size = edge->size;
        }
        else if (edge->asTransferDst)
        {
            bufferTo = buffer;
            offsetTo = edge->offset;
            size = edge->size;
        }
    });

    graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {
        if (edge->asTransferSrc)
        {
            textureFrom = texture;
            fromSubresource = edge->subresourceLayer;
        }
        else if (edge->asTransferDst)
        {
            textureTo = texture;
            toSubresource = edge->subresourceLayer;
        }
    });

    if(bufferFrom != nullptr && bufferTo != nullptr)
    {
        command->CopyBuffer( binding_phase_.get_buffer(bufferFrom), offsetFrom,
                            binding_phase_.get_buffer(bufferTo), offsetTo, size);
    }

    if(textureFrom != nullptr && textureTo != nullptr)
    {
        command->CopyTexture(   binding_phase_.get_texture(textureFrom), fromSubresource,
                                binding_phase_.get_texture(textureTo), toSubresource);

        if(pass->generateMip)      // mip链屏障是pass内动态屏障，保持手工发射
        {
            RHITextureBarrier barrier = {
                .texture = binding_phase_.get_texture(textureTo),
                .srcState = RESOURCE_STATE_TRANSFER_DST,
                .dstState = RESOURCE_STATE_TRANSFER_SRC,
                .subresource = {}
            };
            command->TextureBarrier(barrier);
            command->GenerateMips(binding_phase_.get_texture(textureTo)); // 默认纹理处于src状态，需要手动加屏障

            barrier = {
                .texture = binding_phase_.get_texture(textureTo),
                .srcState = RESOURCE_STATE_TRANSFER_SRC,
                .dstState = RESOURCE_STATE_TRANSFER_DST,
                .subresource = {}
            };
            command->TextureBarrier(barrier);
        }
    }
}

// 屏障发射 /////////////////////////////////////////////////////////////////////

// 悬空起点辅助（刀2b）：挂靠判定=时间线对象匹配（悬空节点的signal发在其挂靠队列的时间线上）
bool PassExecutionPhase::queue_has_dangling(uint32_t q, PerFrameCommonResourceRef executor) const
{
    return queue_dangling_node(q, executor) != nullptr;
}

const RDGSyncNode* PassExecutionPhase::queue_dangling_node(uint32_t q, PerFrameCommonResourceRef executor) const
{
    const RHISemaphoreRef& timeline = executor->queueSlots[q].timeline;
    for (const auto& [family, node] : barrier_generation_phase_.get_dangling_starts())
        if (node.signal.first == timeline)
            return &node;
    return nullptr;
}

void PassExecutionPhase::emit_barrier(RHICommandListRef command, const RDGBarrier& barrier)
{
    if (barrier.resource == nullptr) return;

    if (barrier.resource->NodeType() == RDG_RESOURCE_NODE_TYPE_TEXTURE)
    {
        // 诊断：句柄映射（与validation错误的VkImage句柄对账用）
        command->TextureBarrier({
            binding_phase_.get_texture(static_cast<RDGTextureNodeRef>(barrier.resource)),
            barrier.before_state,
            barrier.after_state,
            barrier.subresource,
            barrier.src_family,
            barrier.dst_family });
    }
    else
    {
        command->BufferBarrier({
            binding_phase_.get_buffer(static_cast<RDGBufferNodeRef>(barrier.resource)),
            barrier.before_state,
            barrier.after_state,
            barrier.buffer_offset,
            barrier.buffer_size });
    }
    recording_result_.total_barriers_inserted.fetch_add(1, std::memory_order_relaxed);
}

void PassExecutionPhase::insert_pass_barriers(RHICommandListRef command, RDGPassNodeRef pass, bool after)
{
    const RDGSyncNode* node = barrier_generation_phase_.get_pass_sync_node(pass);
    if (node == nullptr) return;

    for (const auto& batch : node->batches)
    {
        for (const auto& barrier : batch.barriers)
        {
            if (barrier.after_pass != after) continue;
            emit_barrier(command, barrier);
        }
    }
}

void PassExecutionPhase::collect_in_frame_acquire_guards(RDGPassNodeRef pass,std::vector<std::pair<RDGPassNodeRef, uint32_t>>& guards) const
{
    guards.clear();
    const RDGSyncNode* node = barrier_generation_phase_.get_pass_sync_node(pass);
    if (node == nullptr) return;

    // 帧内acquire（after_pass=false且source_pass有效）：source_pass即配对release的挂载pass
    //（发射侧：release挂tracker.last_pass之后、acquire挂消费者之前，二者source_pass同源），
    // source_queue即release所在队列。帧首acquire（source_pass==nullptr，配对release=悬空
    // 节点的prologue屏障，刀C起import同走此路）以哨兵(nullptr,queue)上报——StepB据此把
    // 含它的pass切出sortKey=0的prologue批（prologue批间互序无定义，acquire须在topo+1段）
    for (const auto& batch : node->batches)
        for (const RDGBarrier& barrier : batch.barriers)
            if (!barrier.after_pass &&
                barrier.src_family != RHI_QUEUE_FAMILY_IGNORED &&
                barrier.dst_family != RHI_QUEUE_FAMILY_IGNORED)
                guards.emplace_back(barrier.source_pass, barrier.source_queue);
}

// 释放 /////////////////////////////////////////////////////////////////////////

void PassExecutionPhase::release_sweep(RDGDependencyGraphRef graph, const std::vector<RDGPassNodeRef>& orderedPasses)
{
    // 录制期不碰任何池（RDG池无锁，不能并发）——全部录制完成后按拓扑全序统一执行last-use释放。
    // 释放只是CPU侧池条目归还（下一帧分配才消费），时序后移不影响本帧GPU行为
    for (RDGPassNodeRef pass : orderedPasses)
        release_at_last_use(graph, pass);
}

void PassExecutionPhase::release_at_last_use(RDGDependencyGraphRef graph, RDGPassNodeRef pass)
{
    // 归还族=tracker终态真末族（池化悬空承接制 2026-10-26，Q4记族）：下帧首触同族零转移、
    // 跨族由悬空起点承接（种子族=池记族）。import不在此路径（release_texture短路IsImported）。
    // CONCURRENT恒IGNORED
    graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {
        // 记录tracker终态（GPU实际布局）而非边声明状态——池化跨帧状态必须与实际一致，
        // 否则下一帧initState错位会漏屏障（layout错误）
        if (binding_phase_.is_last_use(texture, pass, edge->IsOutput()))
        {
            const uint32_t fam = texture->info.sharingMode == RESOURCE_SHARING_TYPE_CONCURRENT
                ? RHI_QUEUE_FAMILY_IGNORED : barrier_generation_phase_.get_final_texture_family(texture);
            release_texture(texture, barrier_generation_phase_.get_final_texture_state(texture), fam);
        }
        });

    graph->ForEachBuffer(pass, [&](RDGBufferEdgeRef edge, RDGBufferNodeRef buffer) {
        if (binding_phase_.is_last_use(buffer, pass, edge->IsOutput()))
            release_buffer(buffer, barrier_generation_phase_.get_final_buffer_state(buffer));
        });

    if (const PassBindInfo* bind_info = binding_phase_.get_pass_bind_info(pass))
    {
        for (auto& view : bind_info->pooled_views)
            RDGTextureViewPool::Get(EngineContext::ThreadPool()->ThreadFrameIndex())->Release({view});
    }
}

void PassExecutionPhase::release_texture(RDGTextureNodeRef textureNode, RHIResourceState state, uint32_t queueFamily)
{
    if(textureNode->IsImported()) return;
    if(textureNode->texture)
    {
        RDGTexturePool::Get(EngineContext::ThreadPool()->ThreadFrameIndex())->Release({ textureNode->texture, state, queueFamily });
        textureNode->texture = nullptr;
        textureNode->initState = RESOURCE_STATE_UNDEFINED;
        textureNode->initFamily = RHI_QUEUE_FAMILY_IGNORED;
    }
}

void PassExecutionPhase::release_buffer(RDGBufferNodeRef bufferNode, RHIResourceState state)
{
    if(bufferNode->IsImported()) return;
    if(bufferNode->buffer)
    {
        RDGBufferPool::Get(EngineContext::ThreadPool()->ThreadFrameIndex())->Release({ bufferNode->buffer, state});
        bufferNode->buffer = nullptr;
        bufferNode->initState = RESOURCE_STATE_UNDEFINED;
    }
}

void PassExecutionPhase::release_pooled_descriptor_sets(RDGDependencyGraphRef graph)
{
    auto& passes = get_passes(graph);
    for (auto& pass : passes)
    {
        if (!pass) continue;

        const PassBindInfo* bind_info = binding_phase_.get_pass_bind_info(pass);
        if (bind_info == nullptr) continue;      // 池化的view在pass结束后就可以释放，但是描述符得全部执行完再释放？

        for (auto& descriptor : bind_info->pooled_descriptor_sets)
        {
            // 归还键与分配键同源(set2足迹/set0,1旧键,与ensure侧镜像,永不发散)
            RDGDescriptorSetPool::Get(EngineContext::ThreadPool()->ThreadFrameIndex())
                ->Release(descriptor.second == 2 ? RDGDescriptorSetPool::Key::MakeKey(pass->set2Entries, descriptor.second)
                                                : RDGDescriptorSetPool::Key::MakeLegacyKey(pass->rootSignature->GetInfo(), descriptor.second),
                          {descriptor.first});
        }
    }
}

// 工具 /////////////////////////////////////////////////////////////////////////

std::array<RHIDescriptorSetRef, MAX_DESCRIPTOR_SETS> PassExecutionPhase::build_descriptor_array(RDGPassNodeRef pass) const
{
    std::array<RHIDescriptorSetRef, MAX_DESCRIPTOR_SETS> descriptors = {};

    if (const PassBindInfo* bind_info = binding_phase_.get_pass_bind_info(pass))
    {
        for (const auto& [set, descriptor] : bind_info->descriptor_sets)
        {
            if (set < MAX_DESCRIPTOR_SETS) descriptors[set] = descriptor;
        }
    }

    return descriptors;
}
