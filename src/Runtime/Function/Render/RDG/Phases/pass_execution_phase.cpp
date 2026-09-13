#include "pass_execution_phase.h"

#include "Function/Global/Definations.h"
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
    const CrossQueueSyncAnalysis& sync_analysis,
    const BarrierGenerationPhase& barrier_generation_phase,
    const PassBindingPhase& binding_phase,
    RDGCrossFrameRegistry& cross_frame_registry,
    const CommandRecordingConfig& config)
    : config_(config)
    , queue_schedule_(queue_schedule)
    , reorder_phase_(reorder_phase)
    , sync_analysis_(sync_analysis)
    , barrier_generation_phase_(barrier_generation_phase)
    , binding_phase_(binding_phase)
    , cross_frame_registry_(cross_frame_registry)
{
}

void PassExecutionPhase::reset_for_frame()
{
    recording_result_.total_passes_executed.store(0, std::memory_order_relaxed);
    recording_result_.total_barriers_inserted.store(0, std::memory_order_relaxed);
    recording_result_.total_sync_points_processed = 0;
    recording_result_.total_submit_batches = 0;

    orderedPasses_.clear();
    topoIndex_.clear();
    streams_.clear();
    planBatchIndex_.clear();
    activeQueues_.clear();
    batches_.clear();
    activePoints_.clear();
    producerPoints_.clear();
    consumerPoints_.clear();
    pointValues_.clear();
    firstTouchKeys_.clear();
    lastUseBatch_.clear();
    homingBatch_.clear();
    lastUseBatches_.clear();
    batchLastUseValue_.clear();
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

    // Step B：同步点驱动的提交批次切分（覆盖串行区）
    split_submit_batches();

    // Step C：提交计划组装 + timeline值按提交序分配
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
        ENGINE_LOG_INFO("[Phase8] passes: {}, barriers: {}, sync points: {}, submit batches: {}",
            recording_result_.total_passes_executed.load(std::memory_order_relaxed),
            recording_result_.total_barriers_inserted.load(std::memory_order_relaxed),
            recording_result_.total_sync_points_processed,
            recording_result_.total_submit_batches);
        ENGINE_LOG_INFO("[Phase8] ===== frame #{} end =====", debugFrame);
    }
}

// Step A ////////////////////////////////////////////////////////////////////////

void PassExecutionPhase::build_queue_streams(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{
    // 规范全局序（剔除culled）——批次排序键与串行边界投影的基准。
    // 两条路径，排序键与流构造序必须一致（否则stable_sort打乱同队列批次、破坏timeline signal
    // 单调性→GPU死锁）：
    //   HEFT：Phase 3的全局调度序（流构造序=调度序的队列投影）——schedule_order非空即用
    //   旧classify：依赖级别序扁平化（与Phase 3构建queue_schedules的遍历一致——Kahn拓扑序
    //     与级别序在同层独立pass上可能不一致；级别序依赖保序：依赖边⇒级别严格递增）

    // 将全局序压入orderedPasses_，并记录其在全局序中的下标（topoIndex_）
    // 用于Step C 的所有队列的提交批次排序 和 串行边界投影
    const TimelineScheduleResult& scheduleForOrder = queue_schedule_.get_schedule_result();
    if (!scheduleForOrder.schedule_order.empty())
    {
        for (RDGPassNodeRef pass : scheduleForOrder.schedule_order)
        {
            if (pass && !pass->isCulled)
            {
                topoIndex_[pass] = static_cast<uint32_t>(orderedPasses_.size());
                orderedPasses_.push_back(pass);
            }
        }
    }
    else
    {
        const LogicalTopologyResult& topology = sync_analysis_.get_dependency_analysis().get_logical_topology_result();
        for (const auto& level : topology.logical_levels)
        {
            for (RDGPassNodeRef pass : level.passes)
            {
                if (pass && !pass->isCulled)
                {
                    topoIndex_[pass] = static_cast<uint32_t>(orderedPasses_.size());
                    orderedPasses_.push_back(pass);
                }
            }
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
        if (!streams_[q].passes.empty()) activeQueues_.push_back(q);

    // 有效同步点
    activePoints_.clear();
    producerPoints_.clear();
    consumerPoints_.clear();
    for (const auto& point : sync_analysis_.get_optimized_sync_points())
    {
        // 悬空过滤（生产者/消费者被cull则该依赖已随pass消失）
        if (point.producer_pass == nullptr || point.consumer_pass == nullptr) continue;
        if (point.producer_pass->isCulled || point.consumer_pass->isCulled) continue;

        const uint32_t index = static_cast<uint32_t>(activePoints_.size());
        activePoints_.push_back(point);
        producerPoints_[point.producer_pass].push_back(index);
        consumerPoints_[point.consumer_pass].push_back(index);
    }
    recording_result_.total_sync_points_processed = static_cast<uint32_t>(activePoints_.size());

    if (config_.enable_debug_output)
    {
        ENGINE_LOG_INFO("[StepA] queues: total={} active={} | orderedPasses={} | sync points: raw={} active={}",
            streams_.size(), activeQueues_.size(), orderedPasses_.size(),
            sync_analysis_.get_optimized_sync_points().size(), activePoints_.size());
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

void PassExecutionPhase::split_submit_batches()
{
    batches_.assign(streams_.size(), {});

    for (uint32_t q : activeQueues_)
    {
        const QueueStream& stream = streams_[q];
        std::vector<StreamBatch>& queueBatches = batches_[q];

        // 支配覆盖表：源队列 → 本队列更早批次已等待生产者的最大canonical位置
        //（仅批首pass的consumer点会真正发wait——批创建时折入；见Step C的值级去重与覆盖审计）
        std::unordered_map<uint32_t, uint32_t> coveredMaxPos;

        StreamBatch current;
        current.begin = 0;
        bool currentOpen = false;
        uint32_t headTopo = 0;                          // 当前批首pass的canonical位置
        const char* pendingOpenReason = "stream-start"; // 下一批的开批原因（日志用）

        // 遍历流中的每一个pass
        for (uint32_t i = 0; i < stream.passes.size(); i++)
        {
            RDGPassNodeRef pass = stream.passes[i];

            // 统一守卫模型（见头文件Step B文档）。两类守卫、两种覆盖判据——【正交，不可互换】：
            //   consumer点守卫（数据就绪）：只能由支配覆盖——本队列更早批已等过sq上≥pos的生产者
            //     （coveredMaxPos[sq]≥pos ⇒ sq提交序串行 ⇒ pos所在批已完成）。跨队列的提交序
            //     不蕴含执行序，不可作为数据就绪的依据。
            //   帧内acquire守卫（QFO配对序）：只能由提交序覆盖——headTopo>pos ⇒ 本批提交键
            //     恒大于pos所在批键≤canonical(pos)。其执行序由同pass的consumer点守卫蕴含
            //     （下方蕴含断言：acquire者必为consumer，且SSIS取最大使点位置≥release位置）。
            // 守卫 = consumer点（pos=点生产者canonical，sq=生产者队列）∪ 帧内跨族acquire
            //（pos=配对release所在pass的canonical，sq=release队列）；帧首acquire的配对release
            // 在prologue批（恒为计划首）——提交序天然覆盖，不构成守卫。
            // 由此：旧acquire开批规则被蕴含（未覆盖时开批使批头=本pass，配对序+执行序同时成立）；
            // release收批删除——配对序由acquire侧检查独立保证（见下方QFO配对序断言）。
            // producer收批保留：signal挂批尾是粒度语义（粗化属第二层，见schedule_reorder.cpp备忘）
            if (currentOpen)
            {
                // 必须再次开批
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
                    // consumer点守卫：数据就绪【只能】由支配覆盖——跨队列的提交序不蕴含执行序，
                    // 未被支配覆盖的consumer必须开批发wait（否则读到未写完的数据）

                    // 如果这个pass是有效同步点中的消费pass
                    if (auto consumerFound = consumerPoints_.find(pass); consumerFound != consumerPoints_.end())
                    {
                        // 取到与他相关的所有有效同步点
                        for (uint32_t pointIndex : consumerFound->second)
                        {
                            // 该同步点的生产者pass在全局拓扑序中的位置
                            auto posIt = topoIndex_.find(activePoints_[pointIndex].producer_pass);
                            if (posIt == topoIndex_.end()) continue;
                            // 在需要同步的生产者队列上，已经同步过的生产者位置 < 该同步点的生产者位置，说明需要新的同步。
                            // 覆盖表无条目=该源队列从未等待过（必须开批）——不能用operator[]默认0
                            // 判断：canonical位置0的生产者（帧首pass）会因0<0恒假被漏掉（HEFT把帧首
                            // pass跨队列化时暴露为culling竞态闪烁，2026-09-11审计②抓获）
                            auto coveredIt = coveredMaxPos.find(activePoints_[pointIndex].producer_queue_index);
                            if (coveredIt == coveredMaxPos.end() || coveredIt->second < posIt->second)
                            {
                                mustOpen = true;
                                reason = "guard-consumer";
                                break;
                            }
                        }
                    }

                    if (!mustOpen)
                    {
                        // 帧内acquire守卫：QFO配对序【只能】由提交序覆盖——批头canonical≤release位置
                        // 则必须开批（开批使批头=本pass，其consumer点的wait同时承接执行序；数据就绪
                        // 侧由上方consumer守卫独立保证，两类守卫判据正交不可互换——见函数头注释）
                        std::vector<std::pair<RDGPassNodeRef, uint32_t>> acquireGuards;
                        collect_in_frame_acquire_guards(pass, acquireGuards);
                        for (const auto& guard : acquireGuards)
                        {
                            auto posIt = topoIndex_.find(guard.first);
                            if (posIt == topoIndex_.end()) continue;
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
                pendingOpenReason = "after-producer-close";

                auto headIt = topoIndex_.find(pass);
                assert(headIt != topoIndex_.end());
                headTopo = headIt->second;

                // 批首pass的consumer点将真正发wait（Step C值级去重后）——折入支配覆盖表。
                // acquire守卫不发wait：其执行覆盖蕴含自同pass的consumer点（见函数头蕴含论证）
                auto foldFound = consumerPoints_.find(pass);
                if (foldFound != consumerPoints_.end())
                {
                    for (uint32_t pointIndex : foldFound->second)
                    {
                        auto posIt = topoIndex_.find(activePoints_[pointIndex].producer_pass);
                        if (posIt == topoIndex_.end()) continue;
                        uint32_t& covered = coveredMaxPos[activePoints_[pointIndex].producer_queue_index];
                        covered = std::max(covered, posIt->second);
                    }
                }
            }

            current.hasPresent = current.hasPresent || pass->NodeType() == RDG_PASS_NODE_TYPE_PRESENT;

            // signal规则：producer所在的批在其后收口（signal挂批次末尾，等待者不会过早放行）。
            // release收批规则已删除——统一守卫模型下配对序由acquire侧开批检查独立保证
            if (producerPoints_.count(pass) > 0)
            {
                current.end = i + 1;
                queueBatches.push_back(current);
                currentOpen = false;
                pendingOpenReason = "after-producer-close";
            }
        }

        if (currentOpen)
        {
            current.end = static_cast<uint32_t>(stream.passes.size());
            queueBatches.push_back(current);
        }

        // 串行区标记
        for (auto& batch : queueBatches)
            // 整个流都没有串行区或者批首pass在串行区起点之后
            batch.serial = (stream.serialPos < stream.passes.size() && batch.begin >= stream.serialPos);
    }

    // 蕴含断言（统一守卫模型的执行覆盖支柱）：帧内跨族acquire者必为某同步点的consumer
    // （跨族⇒跨队列⇒边⇒SSIS点，且SSIS只合并producer端、consumer保留）——由此acquire守卫
    // （release位置）被同pass的consumer点（位置≥release位置，SSIS取最大）蕴含：批首点发wait
    // 覆盖release执行；被合并pass的覆盖由支配表传递（见split主循环注释）。
    // 帧首acquire（source_pass==nullptr）无帧内生产者，由prologue机制承接，不在此列。
    // 若此断言被击穿说明acquire守卫失去wait锚点。审计①，统一开关ENABLE_RDG_AUDIT（Definations.h）
#if ENABLE_RDG_AUDIT
    for (uint32_t q : activeQueues_)
        for (RDGPassNodeRef pass : streams_[q].passes)
        {
            const std::vector<BarrierBatch>* batches = barrier_generation_phase_.get_pass_barrier_batches(pass);
            if (batches == nullptr) continue;
            bool hasInFrameAcquire = false;
            for (const auto& batch : *batches)
                for (const RDGBarrier& barrier : batch.barriers)
                    if (!barrier.after_pass && barrier.source_pass != nullptr &&
                        barrier.src_family != RHI_QUEUE_FAMILY_IGNORED && barrier.dst_family != RHI_QUEUE_FAMILY_IGNORED)
                        hasInFrameAcquire = true;
            if (hasInFrameAcquire)
                assert(consumerPoints_.count(pass) > 0);
        }
#endif

    // QFO配对序契约（统一守卫模型的运行时证明）：每个帧内acquire所在批的批头canonical >
    // 配对release所在pass的canonical ⇒ release批键（=其批头canonical≤release pass canonical）
    // < acquire批键（=批头canonical）——验证层"release先于acquire提交"成立。
    // 审计②，统一开关ENABLE_RDG_AUDIT（Definations.h）
#if ENABLE_RDG_AUDIT
    for (uint32_t q : activeQueues_)
        for (const StreamBatch& batch : batches_[q])
        {
            const uint32_t headTopo = topoIndex_[streams_[q].passes[batch.begin]];
            for (uint32_t i = batch.begin; i < batch.end; i++)
            {
                std::vector<std::pair<RDGPassNodeRef, uint32_t>> guards;
                collect_in_frame_acquire_guards(streams_[q].passes[i], guards);
                for (const auto& guard : guards)
                {
                    auto posIt = topoIndex_.find(guard.first);
                    assert(posIt != topoIndex_.end());
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
                // 开批原因=split主循环记录值；收批原因复算（producer收批或被下一开批蕴含）
                std::string closeReason = "stream-end";
                if (batch.end < stream.passes.size())
                {
                    RDGPassNodeRef last = stream.passes[batch.end - 1];
                    if (producerPoints_.count(last) > 0) closeReason = "producer-signal";
                    else closeReason = "next-open/serial";
                }
                ENGINE_LOG_INFO("[StepB] q{} batch[{}] passes[{}..{}] '{}'..'{}' open={} close={}{}{}",
                    q, b, batch.begin, batch.end - 1,
                    stream.passes[batch.begin]->Name().c_str(),
                    stream.passes[batch.end - 1]->Name().c_str(),
                    batch.openReason, closeReason,
                    batch.serial ? " [serial]" : "", batch.hasPresent ? " [present]" : "");
            }
        }
    }
}

// Step C ////////////////////////////////////////////////////////////////////////

void PassExecutionPhase::compute_imported_touch_sets()
{
    firstTouchKeys_.clear();
    lastUseBatch_.clear();
    homingBatch_.clear();
    lastUseBatches_.clear();

    for (uint32_t q : activeQueues_)
    {
        for (uint32_t b = 0; b < batches_[q].size(); b++)
        {
            const StreamBatch& batch = batches_[q][b];
            for (uint32_t i = batch.begin; i < batch.end; i++)
            {
                RDGPassNodeRef pass = streams_[q].passes[i];

                // 单个imported键触碰登记：本队列首触批（每键一次，wait挂点）+ 本队列末触更新（b单调递增）
                auto touch = [&](const void* key)
                {
                    if (key == nullptr) return;
                    auto& perQueue = lastUseBatch_[key];
                    auto found = perQueue.find(q);
                    if (found == perQueue.end())
                    {
                        perQueue.emplace(q, std::make_pair(q, b));
                        firstTouchKeys_[{q, b}].push_back(key);
                    }
                    else
                        found->second = std::make_pair(q, b);
                };

                // 键=底层RHI对象指针（imported节点Build期已挂对象；跨帧身份与节点/名字无关）
                pass->foreach_textures([&](RDGTextureNodeRef node, RDGTextureEdgeRef)
                    {
                        if (node->IsImported()) touch(binding_phase_.get_texture(node).get());
                    });
                pass->foreach_buffers([&](RDGBufferNodeRef node, RDGBufferEdgeRef)
                    {
                        if (node->IsImported()) touch(binding_phase_.get_buffer(node).get());
                    });

                // 归巢载体批登记（imported与池化纹理皆有归巢——prologue承接wait须全覆盖）：
                // target_pass==nullptr的跨族release即Phase 7归巢循环产物，锚定其真实挂载批
                const std::vector<BarrierBatch>* batches = barrier_generation_phase_.get_pass_barrier_batches(pass);
                if (batches != nullptr)
                {
                    for (const auto& barrierBatch : *batches)
                        for (const RDGBarrier& barrier : barrierBatch.barriers)
                            if (barrier.after_pass && barrier.target_pass == nullptr &&
                                barrier.src_family != RHI_QUEUE_FAMILY_IGNORED && barrier.dst_family != RHI_QUEUE_FAMILY_IGNORED)
                            {
                                const void* key = binding_phase_.get_texture(
                                    static_cast<RDGTextureNodeRef>(barrier.resource)).get();
                                if (key != nullptr)
                                {
                                    homingBatch_[key] = {q, b};
                                    lastUseBatches_.insert({q, b});   // 归巢批也需signal（prologue wait点）
                                }
                            }
                }
            }
        }
    }

    for (const auto& [key, perQueue] : lastUseBatch_)
        for (const auto& [q, qb] : perQueue)
            lastUseBatches_.insert(qb);     // 各队列末触批也要signal（细链wait点全集）
}

void PassExecutionPhase::build_submit_plan(PerFrameCommonResourceRef executor)
{
    RHIQueueSubmitPlan& plan = executor->submitPlan;
    plan.batches.clear();
    pointValues_.clear();

    const TimelineScheduleResult& schedule = queue_schedule_.get_schedule_result();

    // graphics队列下标（Present/swapchain所在；present恒为graphics分类）
    uint32_t graphicsQueue = activeQueues_.front();
    for (uint32_t q : activeQueues_)
        if (schedule.all_queues[q].type == ERenderGraphQueueType::Graphics) { graphicsQueue = q; break; }

    // 帧首prologue：存在帧首跨族acquire时，graphics流起点须先执行配对release(0→G)
    // （见barrier_generation_phase的归巢/prologue机制），并signal一个prologueDone值——
    // 含帧首acquire的批次wait它，保证release先于acquire执行
    const bool needPrologue = !barrier_generation_phase_.get_frame_start_releases().empty();
    uint64_t prologueValue = 0;

    // 细链触碰集（首触批/末触批/全局末触——两模式恒算：细链wait数据源不能因模式切换断代）
    compute_imported_touch_sets();

    // ---- 第一遍：逐队列按批次序分配timeline值（每队列独立时间线，值在该队列的时间线上
    //      随批次序严格递增；同步点的值分配在其生产者队列的时间线上）----
    std::vector<uint64_t> nextValue(streams_.size(), 0);
    for (uint32_t q = 0; q < streams_.size(); q++)
        nextValue[q] = executor->queueSlots[q].timelineValueBase;

    std::vector<uint64_t> queueDoneValue(streams_.size(), 0);

    for (uint32_t q : activeQueues_)
    {
        for (uint32_t b = 0; b < batches_[q].size(); b++)
        {
            const StreamBatch& batch = batches_[q][b];

            if (q == graphicsQueue && b == 0 && needPrologue)
                prologueValue = ++nextValue[q];    // prologueDone（graphics首批次内最先signal）

            for (uint32_t i = batch.begin; i < batch.end; i++)
            {
                auto found = producerPoints_.find(streams_[q].passes[i]);
                if (found == producerPoints_.end()) continue;
                for (uint32_t pointIndex : found->second)
                    pointValues_[pointIndex] = ++nextValue[q];   // 值在生产者队列的时间线上
            }
            // 细链末触signal值：本批是imported键在某队列的末触批（同批多键共享一值，signal数=批数级）
            if (lastUseBatches_.count({q, b}) > 0)
                batchLastUseValue_[{q, b}] = ++nextValue[q];
            if (b == batches_[q].size() - 1)
                queueDoneValue[q] = ++nextValue[q];    // 队列末批：queueDone（join批/单队列fence用）
        }
    }

#if ENABLE_RDG_AUDIT
    // 覆盖审计辅助表（审计③，统一开关ENABLE_RDG_AUDIT）：pass → 其所在批（或其后首个有信号批）的最小信号值。
    // 等待值≥此值即蕴含该批完成：时间线值按批序单调，等待v⇒v所在批完成⇒更早批全完成
    // （队列串行）；同批多信号同时完成，故判据取【批内最小值】而非点自身值——
    // 生产者批内还有更早pass的信号/prologueDone时，等过那些更小值同样覆盖本生产者
    std::unordered_map<RDGPassNodeRef, uint64_t> passCoverValue;
    {
        for (int32_t s = static_cast<int32_t>(activeQueues_.size()) - 1; s >= 0; s--)
        {
            const uint32_t q = activeQueues_[s];
            std::vector<uint64_t> mins(batches_[q].size(), UINT64_MAX);
            for (uint32_t b = 0; b < batches_[q].size(); b++)
            {
                if (q == graphicsQueue && b == 0 && needPrologue)
                    mins[b] = std::min(mins[b], prologueValue);
                for (uint32_t i = batches_[q][b].begin; i < batches_[q][b].end; i++)
                {
                    auto prodFound = producerPoints_.find(streams_[q].passes[i]);
                    if (prodFound == producerPoints_.end()) continue;
                    for (uint32_t pi : prodFound->second)
                        mins[b] = std::min(mins[b], pointValues_[pi]);
                }
                auto luFound = batchLastUseValue_.find({q, b});
                if (luFound != batchLastUseValue_.end())
                    mins[b] = std::min(mins[b], luFound->second);
                if (b == batches_[q].size() - 1)
                    mins[b] = std::min(mins[b], queueDoneValue[q]);
            }
            for (int32_t b = static_cast<int32_t>(batches_[q].size()) - 2; b >= 0; b--)
                mins[b] = std::min(mins[b], mins[b + 1]);
            for (uint32_t b = 0; b < batches_[q].size(); b++)
                for (uint32_t i = batches_[q][b].begin; i < batches_[q][b].end; i++)
                    passCoverValue[streams_[q].passes[i]] = mins[b];
        }
    }
#endif

    // ---- 第二遍：组装waits/signals ----
    // 批次按【首个pass的拓扑序】排序提交（稳定排序保持各队列内部批次序）：
    // 验证层要求所有权转移的release在配对acquire【之前提交】——按队列整块提交时graphics的
    // acquire会先于compute的release入队；拓扑序保证生产者批次恒排在前，同时维持各时间线的
    // signal值随提交序严格递增（队列内相对序不变）
    struct PlanEntry
    {
        uint32_t sortKey;               // 批次首个pass的拓扑序
        uint32_t queue;
        uint32_t batch;
        RHIQueueSubmitBatch submit;
    };
    std::vector<PlanEntry> entries;

    for (uint32_t q : activeQueues_)
    {
        // 值级支配去重表：本队列已等待的各时间线最大值（时间线指针→值）。≤已等值的wait被
        // 本队列提交序串行蕴含（时间线值单调，等过大值蕴含小值已满足）——不发。
        // 与Step B的位置级支配判定构成双保险：两处判定不一致时由批内覆盖审计断言捕获
        std::unordered_map<const void*, uint64_t> waitedMax;

        for (uint32_t b = 0; b < batches_[q].size(); b++)
        {
            const StreamBatch& batch = batches_[q][b];

            RHIQueueSubmitBatch submitBatch;
            submitBatch.queue = executor->queueSlots[q].queue;

            // waits：批次首pass的consumer点。引用【生产者队列】的时间线；waitState=to_state →
            // 提交时映射stage并按等待队列族能力裁剪。值级支配去重（见上方waitedMax注释）
            auto consumerFound = consumerPoints_.find(streams_[q].passes[batch.begin]);
            if (consumerFound != consumerPoints_.end())
            {
                for (uint32_t pointIndex : consumerFound->second)
                {
                    const RHISemaphoreRef& producerTimeline =
                        executor->queueSlots[activePoints_[pointIndex].producer_queue_index].timeline;
                    uint64_t& maxWaited = waitedMax[producerTimeline.get()];
                    if (pointValues_[pointIndex] <= maxWaited) continue;
                    maxWaited = pointValues_[pointIndex];
                    RHISemaphoreWaitInfo wait{};
                    wait.semaphore = producerTimeline;
                    wait.value = pointValues_[pointIndex];
                    wait.isTimeline = true;
                    wait.waitState = activePoints_[pointIndex].to_state;
                    submitBatch.waits.push_back(wait);
                }
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

            // 跨帧同步（消除imported持久资源的跨帧WAR竞态；同队列无需——VkQueue提交序天然串行）：
            // 细链——本批首触的imported键，wait其上帧【各队列】末触点（按timeline聚合取max）；
            //   graphics首批含prologue时追加wait上帧全部归巢点（prologue acquire(F→GFX)的
            //   配对release=上帧归巢，执行序承接精确到归巢批而非整队列queueDone）。
            //   未命中=上帧未触碰（FRAMES_IN_FLIGHT=2的fence链归纳：更早帧已完成，安全跳过）。
            // 粗链——本队列首批次wait上帧其他队列的queueDone（帧间全序串行，A/B回退用）
            if (cross_frame_registry_.has_last_frame() && !config_.coarse_cross_frame_sync)
            {
                // timeline指针→(信号量引用, max值)——同时间线多wait点聚合为最大值
                std::unordered_map<const void*, std::pair<RHISemaphoreRef, uint64_t>> crossFrameWaits;
                auto addCrossFrameWait = [&](const RHISemaphoreRef& timeline, uint64_t value)
                {
                    if (timeline == nullptr || value == 0) return;
                    auto& slot = crossFrameWaits[timeline.get()];
                    slot.first = timeline;
                    slot.second = std::max(slot.second, value);
                };

                auto firstFound = firstTouchKeys_.find({q, b});
                if (firstFound != firstTouchKeys_.end())
                {
                    for (const void* key : firstFound->second)
                    {
                        const RDGCrossFrameRegistry::ResourceRecord* record = cross_frame_registry_.find_resource(key);
                        if (record == nullptr) continue;
                        for (const auto& [sourceQueue, point] : record->lastUsePerQueue)
                            if (sourceQueue != q)
                                addCrossFrameWait(point.timeline, point.value);
                    }
                }

                if (needPrologue && q == graphicsQueue && b == 0)
                {
                    cross_frame_registry_.foreach_resource([&](const void*, const RDGCrossFrameRegistry::ResourceRecord& record)
                        {
                            addCrossFrameWait(record.homingPoint.timeline, record.homingPoint.value);
                        });
                }

                for (const auto& [timeline, waitPair] : crossFrameWaits)
                {
                    RHISemaphoreWaitInfo wait{};
                    wait.semaphore = waitPair.first;
                    wait.value = waitPair.second;
                    wait.isTimeline = true;
                    wait.waitState = RESOURCE_STATE_UNDEFINED;    // 宽掩码：跨帧一切用途都须等待
                    submitBatch.waits.push_back(wait);
                }
            }
            else if (b == 0 && cross_frame_registry_.has_last_frame())
            {
                for (uint32_t s = 0; s < streams_.size(); s++)
                {
                    const RDGCrossFrameRegistry::QueueRecord* lastFrame = cross_frame_registry_.find_queue(s);
                    if (s == q || lastFrame == nullptr || lastFrame->timeline == nullptr || lastFrame->doneValue == 0) continue;

                    RHISemaphoreWaitInfo wait{};
                    wait.semaphore = lastFrame->timeline;
                    wait.value = lastFrame->doneValue;
                    wait.isTimeline = true;
                    wait.waitState = RESOURCE_STATE_UNDEFINED;    // 宽掩码：帧首一切用途都须等待
                    submitBatch.waits.push_back(wait);
                }
            }

            // 含帧首跨族acquire的批次：wait graphics prologueDone（保证prologue release先执行；
            // 对整批wait是过同步但安全——acquire可能位于批内任意pass之前）。
            // 支配去重：prologueDone是graphics时间线本帧最小值，本队列首个含帧首acquire的批
            // 发出即覆盖全队列（提交序串行蕴含后续批次）
            if (needPrologue && q != graphicsQueue)
            {
                for (uint32_t i = batch.begin; i < batch.end; i++)
                {
                    if (pass_has_frame_start_acquire(streams_[q].passes[i]))
                    {
                        uint64_t& maxWaited = waitedMax[executor->queueSlots[graphicsQueue].timeline.get()];
                        if (prologueValue > maxWaited)
                        {
                            RHISemaphoreWaitInfo wait{};
                            wait.semaphore = executor->queueSlots[graphicsQueue].timeline;
                            wait.value = prologueValue;
                            wait.isTimeline = true;
                            wait.waitState = RESOURCE_STATE_SHADER_RESOURCE;
                            submitBatch.waits.push_back(wait);
                            maxWaited = std::max(maxWaited, prologueValue);
                        }
                        break;      // 每批至多一个prologueDone wait
                    }
                }
            }

            // 批内覆盖审计（审计④，统一开关ENABLE_RDG_AUDIT；Step B位置级支配判定的值级复核）：
            // 被合并pass的consumer点与帧首acquire守卫，其所需等待必须已被本批头wait（刚折入
            // waitedMax）或更早批wait覆盖。两级判定不一致即断言——Step B误合并会使内点wait
            // 凭空消失，此处兜底捕获
#if ENABLE_RDG_AUDIT
            for (uint32_t i = batch.begin + 1; i < batch.end; i++)
            {
                RDGPassNodeRef interior = streams_[q].passes[i];
                auto interiorFound = consumerPoints_.find(interior);
                if (interiorFound != consumerPoints_.end())
                {
                    for (uint32_t pointIndex : interiorFound->second)
                    {
                        const RHISemaphoreRef& producerTimeline =
                            executor->queueSlots[activePoints_[pointIndex].producer_queue_index].timeline;
                        const uint64_t coverValue = passCoverValue[activePoints_[pointIndex].producer_pass];
                        if (waitedMax[producerTimeline.get()] < coverValue)
                            ENGINE_LOG_WARN("[GuardAudit] uncovered interior point: q{} batch[{}] interior'{}' point#{} producer'{}'(q{}) value={} coverValue={} maxWaited={}",
                                q, b, interior->Name().c_str(), pointIndex,
                                activePoints_[pointIndex].producer_pass->Name().c_str(),
                                activePoints_[pointIndex].producer_queue_index,
                                pointValues_[pointIndex], coverValue,
                                waitedMax[producerTimeline.get()]);
                        assert(waitedMax[producerTimeline.get()] >= coverValue);
                    }
                }
                if (needPrologue && pass_has_frame_start_acquire(interior))
                    assert(prologueValue <= waitedMax[executor->queueSlots[graphicsQueue].timeline.get()]);
            }
#endif

            // graphics首批次：prologueDone（最先列出——它的值在pass 1中最先分配，为该批最小值，
            // 保持同时间线signal数组按值递增）
            if (q == graphicsQueue && b == 0 && needPrologue)
            {
                RHISemaphoreSubmitInfo signal{};
                signal.semaphore = executor->queueSlots[q].timeline;
                signal.value = prologueValue;
                signal.isTimeline = true;
                submitBatch.signals.push_back(signal);
            }

            // signals：批内所有pass的producer点（producer在批内任意位置——signal挂批次末尾），
            // 全部signal在【本队列】的时间线上（同队批次序→值严格递增；本队批次从不wait自己的时间线）
            for (uint32_t i = batch.begin; i < batch.end; i++)
            {
                auto producerFound = producerPoints_.find(streams_[q].passes[i]);
                if (producerFound == producerPoints_.end()) continue;
                for (uint32_t pointIndex : producerFound->second)
                {
                    RHISemaphoreSubmitInfo signal{};
                    signal.semaphore = executor->queueSlots[q].timeline;
                    signal.value = pointValues_[pointIndex];
                    signal.isTimeline = true;
                    submitBatch.signals.push_back(signal);
                }
            }

            // 细链末触signal：本批为imported键末触批——下一帧（另一槽）首触批wait它。
            // 粗链模式下同样发射（注册表回写在两模式恒做，无人wait的冗余信号无害）
            auto lastUseFound = batchLastUseValue_.find({q, b});
            if (lastUseFound != batchLastUseValue_.end())
            {
                RHISemaphoreSubmitInfo signal{};
                signal.semaphore = executor->queueSlots[q].timeline;
                signal.value = lastUseFound->second;
                signal.isTimeline = true;
                submitBatch.signals.push_back(signal);
            }

            // 队列末批：queueDone；Present批：finishSemaphore（present等待它）
            if (b == batches_[q].size() - 1 && queueDoneValue[q] > 0)
            {
                RHISemaphoreSubmitInfo signal{};
                signal.semaphore = executor->queueSlots[q].timeline;
                signal.value = queueDoneValue[q];
                signal.isTimeline = true;
                submitBatch.signals.push_back(signal);
            }
            if (batch.hasPresent)
            {
                RHISemaphoreSubmitInfo signal{};
                signal.semaphore = executor->finishSemaphore;
                signal.isTimeline = false;
                submitBatch.signals.push_back(signal);
            }

            PlanEntry entry;
            // 排序键=批次首pass的规范序。QFO配对序由守卫开批规则保证：跨族acquire所在批的批头
            // canonical>配对release位置⇒release批键<acquire批键（split_submit_batches末有断言）。
            // prologue批（graphics首批次，含帧首release集）强制最小键——帧首acquire者即使规范序
            // 极早（无依赖的拷贝pass）也必须在其后提交
            if (q == graphicsQueue && b == 0 && needPrologue)
                entry.sortKey = 0;
            else
                entry.sortKey = topoIndex_[streams_[q].passes[batch.begin]];
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
    for (uint32_t i = 0; i < entries.size(); i++)
    {
        planBatchIndex_[entries[i].queue][entries[i].batch] = static_cast<uint32_t>(plan.batches.size());
        plan.batches.push_back(std::move(entries[i].submit));
    }

    // join批：空提交（无命令缓冲），等待全部队列各自时间线上的queueDone → signal帧槽fence。
    // 单队列时省略——fence直接挂该队列末批
    if (activeQueues_.size() > 1)
    {
        RHIQueueSubmitBatch join;
        join.queue = executor->queueSlots[graphicsQueue].queue;
        for (uint32_t q : activeQueues_)
        {
            RHISemaphoreWaitInfo wait{};
            wait.semaphore = executor->queueSlots[q].timeline;
            wait.value = queueDoneValue[q];
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

    // 推进各队列value基（帧槽fence保证：本帧所有signal值在fence signal时已全部终定，
    // 下一帧槽复用（帧首fence->Wait）后旧值不再引用，可安全继续分配）
    for (uint32_t q = 0; q < streams_.size(); q++)
        executor->queueSlots[q].timelineValueBase = nextValue[q] + 1;
    recording_result_.total_submit_batches = static_cast<uint32_t>(plan.batches.size());

    // 更新跨帧注册表：本帧各队列的timeline+queueDone值，供下一帧（另一槽）首批次wait。
    // 未激活队列记空值（跨帧wait按非零过滤）。begin→record→commit即代际换代
    // （失败语义=编译中途失败即fatal，见注册表类注释）
    cross_frame_registry_.begin_frame(static_cast<uint32_t>(streams_.size()));
    for (uint32_t q : activeQueues_)
    {
        RDGCrossFrameRegistry::QueueRecord record;
        record.timeline = executor->queueSlots[q].timeline;
        record.doneValue = queueDoneValue[q];
        cross_frame_registry_.record_queue(q, std::move(record));
    }
    // 资源级回写：imported键每队列末触点 + 归巢载体批signal（下一帧首触wait/prologue承接wait的数据源）
    for (const auto& [key, perQueue] : lastUseBatch_)
    {
        for (const auto& [q, qb] : perQueue)
        {
            RDGCrossFrameRegistry::QueuePoint point;
            point.timeline = executor->queueSlots[q].timeline;
            point.value = batchLastUseValue_[{q, qb.second}];
            cross_frame_registry_.record_resource_last_use(key, q, std::move(point));
        }
    }
    for (const auto& [key, qb] : homingBatch_)
    {
        auto value = batchLastUseValue_.find(qb);
        if (value != batchLastUseValue_.end())
        {
            RDGCrossFrameRegistry::QueuePoint point;
            point.timeline = executor->queueSlots[qb.first].timeline;
            point.value = value->second;
            cross_frame_registry_.record_homing_point(key, std::move(point));
        }
    }
    cross_frame_registry_.commit_frame();

    // 计划自检（审计⑤，统一开关ENABLE_RDG_AUDIT）——构造性不变量：startSemaphore恰一wait
    // （graphics首批次）、finishSemaphore恰一signal（Present批）、同一时间线的signal值按提交序
    // 严格递增且无重复、同一批次内同时间线的signal值大于wait值（Vulkan单提交内约束）、
    // 每个同步点与每个queueDone各signal一次
#if ENABLE_RDG_AUDIT
    {
        uint32_t startWaits = 0, finishSignals = 0;
        std::unordered_map<const void*, uint64_t> lastSignalValuePerTimeline;          // timeline→最近signal值（提交序）
        std::map<std::pair<const void*, uint64_t>, uint32_t> timelineSignalCounts;     // (timeline,value)→次数
        for (const RHIQueueSubmitBatch& batch : plan.batches)
        {
            // 本批次内各时间线的wait值上界（signal值必须超过它）
            std::unordered_map<const void*, uint64_t> waitValuePerTimeline;
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
                }
            }
        }
        assert(startWaits == 1);       // Present恒存在→graphics首批次恒存在
        assert(finishSignals == 1);
        for (const auto& [key, count] : timelineSignalCounts) assert(count == 1);
        // 同步点 + 细链末触 + queueDone + （如启用）prologueDone（粗链回退模式末触signal仍发射，
        // 见wait组装处的两模式恒做说明，故断言公式无需分支）
        assert(timelineSignalCounts.size() == pointValues_.size() + batchLastUseValue_.size() + activeQueues_.size() + (needPrologue ? 1 : 0));
    }
#endif

    if (config_.enable_debug_output)
    {
        // 按提交序dump整帧计划（每行=一次vkQueueSubmit2，含末尾join批）。wait/signal按语义
        // 标注来源与值，便于审计哪些等待/信号是多余的。waitState决定后端推导的stage掩码：
        // WIDE=UNDEFINED宽掩码(COLOR|COMPUTE|TRANSFER)，数字=具体资源态映射的窄掩码
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
            // 末触signal值集合（打印标注用）
            std::set<uint64_t> lastUseValues;
            for (const auto& [qb, v] : batchLastUseValue_)
                if (qb.first == q) lastUseValues.insert(v);
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
                {
                    std::string kind = "syncPt";
                    if (signal.value == queueDoneValue[q]) kind = "queueDone";
                    else if (needPrologue && q == graphicsQueue && signal.value == prologueValue) kind = "prologueDone";
                    else if (lastUseValues.count(signal.value) > 0) kind = "lastUse";
                    signals += kind + "(v" + std::to_string(signal.value) + ") ";
                }
            }
            if (batch.signalFence != nullptr) totalFences++;

            ENGINE_LOG_INFO("[StepC] plan[{}] q{} waits[{}]: {}| signals[{}]: {}| fence={}",
                i, q, batch.waits.size(), waits, batch.signals.size(), signals,
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
    //    this及成员引用的生命期由join保证）
    const auto& frameStartReleases = barrier_generation_phase_.get_frame_start_releases();
    for (uint32_t p = 0; p < pieces.size(); p++)
    {
        RHICommandListRef command = pieceCommands[p];
        const uint32_t q = pieces[p].queue;
        const uint32_t begin = pieces[p].begin;
        const uint32_t end = pieces[p].end;
        // 帧首prologue：graphics流的第一个piece在自身range之前先录release(graphics→G)集
        // （all_queues[0]恒为graphics；begin==0即流首）
        const bool prologuePiece = !frameStartReleases.empty() && q == 0 && begin == 0;
        EngineContext::ThreadPool()->AddQueuedWork([this, graph, executor, q, begin, end, command, prologuePiece]()
        {
            command->BeginCommand();
            if (prologuePiece)
                for (const RDGBarrier& barrier : barrier_generation_phase_.get_frame_start_releases())
                    emit_barrier(command, barrier);
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
        for (StreamBatch& batch : batches_[q])
        {
            if (!batch.serial) continue;
            RHICommandListRef command = serialCommands[q][serialCursor++];
            command->BeginCommand();
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
            executor->renderQuery->WriteTimestamp(command,
                EngineContext::ThreadPool()->ThreadFrameIndex(), queueIndex, 2 * i,
                stream.passes[i]->Name() + " wait");
        execute_pass(graph, executor, queueIndex, stream.passes[i], command);
        if (config_.enable_gpu_timing && executor->renderQuery != nullptr)
            executor->renderQuery->WriteTimestamp(command,
                EngineContext::ThreadPool()->ThreadFrameIndex(), queueIndex, 2 * i + 1,
                stream.passes[i]->Name());
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
        const auto* barrierBatches = barrier_generation_phase_.get_pass_barrier_batches(pass);
        uint32_t before = 0, after = 0;
        if (barrierBatches)
            for (const auto& batch : *barrierBatches)
                for (const auto& barrier : batch.barriers)
                    (barrier.after_pass ? after : before)++;
        ENGINE_LOG_INFO("[Rec] q{} '{}' barriers: before={} after={}", queueIndex, pass->Name().c_str(), before, after);
    }

    // GPU调试标记（颜色与旧路径一致）
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
    const std::vector<BarrierBatch>* batches = barrier_generation_phase_.get_pass_barrier_batches(pass);
    if (batches == nullptr) return;

    for (const auto& batch : *batches)
    {
        for (const auto& barrier : batch.barriers)
        {
            if (barrier.after_pass != after) continue;
            emit_barrier(command, barrier);
        }
    }
}

bool PassExecutionPhase::pass_has_frame_start_acquire(RDGPassNodeRef pass) const
{
    const std::vector<BarrierBatch>* batches = barrier_generation_phase_.get_pass_barrier_batches(pass);
    if (batches == nullptr) return false;

    for (const auto& batch : *batches)
        for (const auto& barrier : batch.barriers)
            if (barrier.source_pass == nullptr && barrier.src_family != RHI_QUEUE_FAMILY_IGNORED)
                return true;
    return false;
}

void PassExecutionPhase::collect_in_frame_acquire_guards(RDGPassNodeRef pass,std::vector<std::pair<RDGPassNodeRef, uint32_t>>& guards) const
{
    guards.clear();
    const std::vector<BarrierBatch>* batches = barrier_generation_phase_.get_pass_barrier_batches(pass);
    if (batches == nullptr) return;

    // 帧内acquire（after_pass=false且source_pass有效）：source_pass即配对release的挂载pass
    //（发射侧：release挂tracker.last_pass之后、acquire挂消费者之前，二者source_pass同源），
    // source_queue即release所在队列。帧首acquire（source_pass==nullptr）跳过——其配对release
    // 在graphics prologue批，恒为计划首，提交序天然覆盖
    for (const auto& batch : *batches)
        for (const RDGBarrier& barrier : batch.barriers)
            if (!barrier.after_pass && barrier.source_pass != nullptr &&
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
    // 归还纹理时记录的归属族 = 静止归属（graphics族）：Phase 7的帧末归巢已把跨族纹理release回
    // graphics（见barrier_generation_phase），池条目一致地记graphics族——下一帧首用若在异族，
    // 由帧首acquire+graphics prologue release对承接
    const uint32_t restFamily = sync_analysis_.get_queue_family_index(0);   // all_queues[0]恒为graphics

    graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {
        // 记录tracker终态（GPU实际布局）而非边声明状态——池化跨帧状态必须与实际一致，
        // 否则下一帧initState错位会漏屏障（layout错误）
        if (binding_phase_.is_last_use(texture, pass, edge->IsOutput()))
            release_texture(texture, barrier_generation_phase_.get_final_texture_state(texture), restFamily);
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
            RDGDescriptorSetPool::Get(EngineContext::ThreadPool()->ThreadFrameIndex())
                ->Release({descriptor.first}, pass->rootSignature, descriptor.second);
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
