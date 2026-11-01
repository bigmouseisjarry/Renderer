#include "barrier_generation_phase.h"

#include "Function/Global/Definations.h"    // ENABLE_RDG_AUDIT（新模型自检）
#include "Function/Global/EngineContext.h"

// 状态枚举转可读名字（仅日志用）
static const char* resource_state_name(RHIResourceState state)
{
    switch (state)
    {
    case RESOURCE_STATE_UNDEFINED:                return "UNDEFINED";
    case RESOURCE_STATE_COMMON:                   return "COMMON";
    case RESOURCE_STATE_TRANSFER_SRC:             return "TRANSFER_SRC";
    case RESOURCE_STATE_TRANSFER_DST:             return "TRANSFER_DST";
    case RESOURCE_STATE_VERTEX_BUFFER:            return "VERTEX_BUFFER";
    case RESOURCE_STATE_INDEX_BUFFER:             return "INDEX_BUFFER";
    case RESOURCE_STATE_COLOR_ATTACHMENT:         return "COLOR_ATTACHMENT";
    case RESOURCE_STATE_DEPTH_STENCIL_ATTACHMENT: return "DEPTH_STENCIL";
    case RESOURCE_STATE_UNORDERED_ACCESS:         return "UAV";
    case RESOURCE_STATE_SHADER_RESOURCE:          return "SRV";
    case RESOURCE_STATE_INDIRECT_ARGUMENT:        return "INDIRECT";
    case RESOURCE_STATE_PRESENT:                  return "PRESENT";
    case RESOURCE_STATE_ACCELERATION_STRUCTURE:   return "AS";
    default:                                      return "UNKNOWN";
    }
}

void BarrierGenerationPhase::emit_barrier(RDGSyncNode& node, RDGBarrier barrier)
{
    // batches恒单Transition批（EBarrierType现仅一值，双通道合并后屏障统一走本表）——
    // 确保首批存在后入队，"恒用front()"的约定收敛于此单点
    if (node.batches.empty())
        node.batches.push_back({{}, EBarrierType::ResourceTransition});
    node.batches.front().barriers.push_back(std::move(barrier));
    result_.total_barriers++;
}

std::vector<BarrierGenerationPhase::TextureSubrangeGroup> BarrierGenerationPhase::compute_texture_subrange_groups(
    const TextureStateTracker& tracker, const ResourceAccessInfo& access) const
{
    // access范围已是具体值（PassInfoAnalysis展开站完成默认值展开——下游不再兜底）
    const uint32_t mip_base = access.mip_base;
    const uint32_t mip_count = access.mip_count;
    const uint32_t array_base = access.array_base;
    const uint32_t array_count = access.array_count;
    assert(mip_count > 0 && array_count > 0);
    const uint32_t mip_end = mip_base + mip_count;
    const uint32_t array_end = array_base + array_count;

    // 全齐短路（常规路径）：范围内(state,frequency)全部一致→整范围单组单矩形，
    // 屏障条数与旧整范围路径逐字节一致
    {
        const size_t first = tracker.index(mip_base, array_base);
        const RHIResourceState state0 = tracker.states[first];
        const uint32_t freq0 = tracker.state_frequencies[first];
        bool uniform = true;
        for (uint32_t mip = mip_base; uniform && mip < mip_end; mip++)
            for (uint32_t layer = array_base; layer < array_end; layer++)
            {
                const size_t i = tracker.index(mip, layer);
                if (tracker.states[i] != state0 || tracker.state_frequencies[i] != freq0)
                {
                    uniform = false; break;
                }
            }
        if (uniform)
        {
            TextureSubrangeGroup group;
            group.state = state0;
            group.frequency = freq0;
            group.ranges.push_back(TextureSubresourceRange{
                .baseMipLevel = mip_base, .levelCount = mip_count,
                .baseArrayLayer = array_base, .layerCount = array_count });
            return { std::move(group) };
        }
    }

    // 混合范围：行扫描（行内layer连续段）+竖向融合（相邻mip行同layer区间合并）——
    // 组数=(state,frequency)等价类数，条数上界=旧逐子资源补差路径，通常塌缩到个位数
    std::vector<TextureSubrangeGroup> groups;
    for (uint32_t mip = mip_base; mip < mip_end; mip++)
    {
        uint32_t layer = array_base;
        while (layer < array_end)
        {
            const size_t head = tracker.index(mip, layer);
            const RHIResourceState state = tracker.states[head];
            const uint32_t frequency = tracker.state_frequencies[head];

            uint32_t run = 1;
            while (layer + run < array_end)
            {
                const size_t i = tracker.index(mip, layer + run);
                if (tracker.states[i] != state || tracker.state_frequencies[i] != frequency) break;
                run++;
            }

            // 归组（等价类数极少，线性查找；本段内groups不再增长，指针稳定）
            TextureSubrangeGroup* group = nullptr;
            for (auto& g : groups)
                if (g.state == state && g.frequency == frequency) { group = &g; break; }
            if (group == nullptr)
            {
                groups.push_back({});
                group = &groups.back();
                group->state = state;
                group->frequency = frequency;
            }

            // 竖向融合：组尾矩形恰在本行正上方且layer区间一致
            bool merged = false;
            if (!group->ranges.empty())
            {
                TextureSubresourceRange& tail = group->ranges.back();
                if (tail.baseMipLevel + tail.levelCount == mip &&
                    tail.baseArrayLayer == layer && tail.layerCount == run)
                {
                    tail.levelCount++; 
                    merged = true;
                }
            }
            if (!merged)
                group->ranges.push_back(TextureSubresourceRange{
                    .baseMipLevel = mip, .levelCount = 1,
                    .baseArrayLayer = layer, .layerCount = run });

            layer += run;
        }
    }
    return groups;
}

BarrierGenerationPhase::BarrierGenerationPhase(
    const QueueSchedule& queue_schedule,
    const PassBindingPhase& binding_phase,
    const PassInfoAnalysis& pass_info_analysis,
    const ExecutionReorderPhase& reorder_phase,
    RDGCrossFrameRegistry& cross_frame_registry,
    const BarrierGenerationConfig& config)
    : config_(config)
    , queue_schedule_(queue_schedule)
    , binding_phase_(binding_phase)
    , pass_info_analysis_(pass_info_analysis)
    , reorder_phase_(reorder_phase)
    , cross_frame_registry_(cross_frame_registry)
{
}

void BarrierGenerationPhase::reset_for_frame()
{
    result_.total_barriers = 0;
    result_.pass_sync_nodes.clear();
    result_.dangling_starts.clear();

    texture_trackers_.clear();
    buffer_trackers_.clear();
}

void BarrierGenerationPhase::on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{
    ENGINE_TIME_SCOPE(BarrierGenerationPhase::on_execute);

    // 帧首跨族acquire的族归属基准：all_queues[0]恒为graphics（QueryConfiguredQueues顺序）
    const uint32_t graphicsFamily = queue_schedule_.get_queue_family_index(0);

    // 初始化状态跟踪器：
    //   imported → Build期声明的 initState
    //   created  → 池化跨帧状态（阶段6集中分配时由池条目写入 node->initState）
    //   owner_family初值 → 跨帧归属族（created=阶段6写入节点initFamily；imported=graphics族
    //                ——Builder的Import不设族，节点initFamily恒为IGNORED，若沿用则帧首跨族判定
    //                永不触发：上帧末留在异族的imported图（如copy队列做完Depth Copy），本帧
    //                graphics首用时不发acquire、布局转移落在错误的族上——validation布局失配的
    //                来源。与池化资源同语义：帧间静止归属恒为graphics族，配合下方帧末归巢）
    // 物理图双节点守卫：帧内两个RDG节点绑定同一物理纹理即告警
    std::unordered_map<const void*, RDGTextureNodeRef> boundPhysicalTextures;
    graph->ForEachTextureNode([&](RDGTextureNodeRef texture) {
        if (const void* physicalKey = binding_phase_.get_texture(texture).get())
            if (auto [it, inserted] = boundPhysicalTextures.emplace(physicalKey, texture); !inserted)
                ENGINE_LOG_WARN("Two RDG texture nodes bind the same physical texture: '{}' and '{}', please merge them into one node!",
                    it->second->Name().c_str(), texture->Name().c_str());

        TextureStateTracker& tracker = texture_trackers_[texture];
        assert(texture->info.mipLevels > 0 && texture->info.arrayLayers > 0);
        tracker.mip_levels = texture->info.mipLevels;
        tracker.array_layers = texture->info.arrayLayers;
        tracker.states.assign(static_cast<size_t>(tracker.mip_levels) * tracker.array_layers, texture->initState);
        tracker.state_frequencies.assign(tracker.states.size(), SHADER_FREQUENCY_ALL);   // 跨帧使用频率未知=ALL保守（S6）
        // CONCURRENT纹理无队列族所有权概念（双族共享，免QFO往返）：owner恒IGNORED——
        // cross_family判定恒假→不发release/acquire、不归巢，布局转移走else分支（族IGNORED）
        tracker.owner_family = texture->info.sharingMode == RESOURCE_SHARING_TYPE_CONCURRENT
            ? RHI_QUEUE_FAMILY_IGNORED
            : (texture->IsImported() ? graphicsFamily : texture->initFamily);

        // import纹理从registry取上帧末同步状态（含旧时间线记录——本帧wait它们
        // =同槽fence语义，已signal瞬间通过；同时持ref跨帧保活时间线对象）。未命中=上帧未触
        // （registry merge换代保旧记录，真未触即进程首见）→空记录，种子族回退graphicsFamily
        if (texture->IsImported())
        {
            if (const void* key = binding_phase_.get_texture(texture).get())
                if (const RDGCrossFrameRegistry::ResourceSyncRecord* rec = cross_frame_registry_.find_resource_sync(key))
                {
                    tracker.sync_queue = rec->owner_queue;
                    tracker.sync_signals = rec->signals;
                    tracker.owner_family = queue_schedule_.get_queue_family_index(rec->owner_queue);
                    if (rec->final_states.size() == tracker.states.size())
                        tracker.states = rec->final_states;
                }
        }
        else
        {
            // 池化种子：本槽各时间线上代末B的{B,B}快照——槽fence已保证上代全部完成（池按槽隔离，条目只在同槽代际轮回），waits瞬过；
            // 种子的实质作用是给悬空+1提供落账锚点（acquirer据此拿到真实的B+1 wait）与"release排在上代一切使用之后"的保守自文档语义。
            // 布局/族种子来自池条目（initState/initFamily，阶段6写入）
            for (const auto& queueSolt : executor->queueSlots)
            {
				const RHISemaphoreRef& timeline = queueSolt.timeline;
				const uint64_t b = cross_frame_registry_.peek_timeline_counter(timeline.get());
				tracker.sync_signals[timeline] = { b, b };
            }
        }
    });

    graph->ForEachBufferNode([&](RDGBufferNodeRef buffer) {
        BufferStateTracker tracker;
        tracker.last_pass = nullptr;
        tracker.state = buffer->initState;

        if (buffer->IsImported())
        {
            if (const void* key = binding_phase_.get_buffer(buffer).get())
            if (const RDGCrossFrameRegistry::ResourceSyncRecord* rec =
                    cross_frame_registry_.find_resource_sync(key))
            {
                tracker.sync_queue = rec->owner_queue;
                tracker.sync_signals = rec->signals;
            }
        }
        buffer_trackers_[buffer] = std::move(tracker);
    });

    // 悬空起点预构建——走查前定格（signal低位预分配=值序跟随提交序的构造保证）
    prebuild_dangling_starts(executor);

    generate_transition_barriers(graph, executor);

    // import资源tracker终态回写registry（merge换代保未触键旧记录）
    write_back_sync_records(graph);

#if ENABLE_RDG_AUDIT
    // 同一队列时间线上signal按schedule_order严格递增——由B的分配公式
    // 构造保证（signal=max(wait[本线],B[本线])+1且回写B），断言防回归
    {
        std::unordered_map<const void*, uint64_t> lastSignal;
        for (RDGPassNodeRef pass : queue_schedule_.get_schedule_result().schedule_order)
        {
            auto found = result_.pass_sync_nodes.find(pass);
            if (found == result_.pass_sync_nodes.end()) continue;
            const auto& signal = found->second.signal;
            if (signal.first == nullptr) continue;
            auto [it, inserted] = lastSignal.emplace(signal.first.get(), signal.second);
            if (!inserted)
            {
                assert(signal.second > it->second);
                it->second = signal.second;
            }
        }
    }
#endif

    if (config_.enable_debug_output)
        debug_info();

}

const RDGSyncNode* BarrierGenerationPhase::get_pass_sync_node(RDGPassNodeRef pass) const
{
    if (auto found = result_.pass_sync_nodes.find(pass); found != result_.pass_sync_nodes.end())
        return &found->second;
    return nullptr;
}

RHIResourceState BarrierGenerationPhase::get_final_texture_state(RDGTextureNodeRef texture) const
{
    if (auto found = texture_trackers_.find(texture); found != texture_trackers_.end())
        return found->second.states.empty() ? RESOURCE_STATE_UNDEFINED : found->second.states.front();
    return RESOURCE_STATE_UNDEFINED;
}

uint32_t BarrierGenerationPhase::get_final_texture_family(RDGTextureNodeRef texture) const
{
    if (auto found = texture_trackers_.find(texture); found != texture_trackers_.end())
        return found->second.owner_family;
    return RHI_QUEUE_FAMILY_IGNORED;
}

RHIResourceState BarrierGenerationPhase::get_final_buffer_state(RDGBufferNodeRef buffer) const
{
    if (auto found = buffer_trackers_.find(buffer); found != buffer_trackers_.end())
        return found->second.state;
    return RESOURCE_STATE_UNDEFINED;
}

const void BarrierGenerationPhase::debug_info() const
{
    ENGINE_LOG_INFO("BarrierGenerationPhase");

    // ===== 汇总 =====
    uint32_t passesWithBatches = 0;
    for (const auto& [pass, node] : result_.pass_sync_nodes)
        if (!node.batches.empty()) passesWithBatches++;
    ENGINE_LOG_INFO("[Barrier] total barriers: {}, passes with batches: {}",
        result_.total_barriers, passesWithBatches);

    // ===== 逐屏障明细 =====
    // 按拓扑序（即录制/发射顺序）输出，帧间可比；[before]/[after] 与pass命令的相对位置一致
    // [before] = pass之前发射（非output边，pass期间状态）
    // [after]  = pass之后发射（output边，产出收敛状态）
    // (from X) = 上一次触碰该资源的pass（init表示来自资源的初始状态）
    // qA->qB   = source_queue -> target_queue（跨队列时相同状态也会生成屏障）
    const auto& topo_order = queue_schedule_.get_schedule_result().schedule_order;   // 调度序=执行序（与binding一致直取）
    for (RDGPassNodeRef pass : topo_order)
    {
        auto found = result_.pass_sync_nodes.find(pass);
        if (found == result_.pass_sync_nodes.end()) continue;

        for (const auto& batch : found->second.batches)
        {
            for (const auto& barrier : batch.barriers)
            {
                if (barrier.resource == nullptr) continue;

                const char* position = barrier.after_pass ? "after " : "before";
                const char* source_pass_name = barrier.source_pass != nullptr ? barrier.source_pass->Name().c_str() : "init";

                if (barrier.resource->NodeType() == RDG_RESOURCE_NODE_TYPE_TEXTURE)
                {
                    ENGINE_LOG_INFO("[Barrier]   '{}' [{}] q{}->q{} texture '{}': {} -> {} (from '{}'){}",
                        pass->Name().c_str(),
                        position,
                        barrier.source_queue, barrier.target_queue,
                        barrier.resource->Name().c_str(),
                        resource_state_name(barrier.before_state),
                        resource_state_name(barrier.after_state),
                        source_pass_name,
                        barrier.is_subresource ? " [sub]" : "");
                }
                else
                {
                    ENGINE_LOG_INFO("[Barrier]   '{}' [{}] q{}->q{} buffer '{}': {} -> {} (from '{}') off={} size={}",
                        pass->Name().c_str(),
                        position,
                        barrier.source_queue, barrier.target_queue,
                        barrier.resource->Name().c_str(),
                        resource_state_name(barrier.before_state),
                        resource_state_name(barrier.after_state),
                        source_pass_name,
                        barrier.buffer_offset, barrier.buffer_size);
                }
            }
        }
    }

    // ===== 悬空起点 =====
    for (const auto& [family, dangling] : result_.dangling_starts)
    {
        ENGINE_LOG_INFO("[Dangling] family {} signal q'{} val {} waits {} batches {}",
            family,
            dangling.signal.first != nullptr ? "?" : "-",
            dangling.signal.second,
            dangling.waits.size(),
            dangling.batches.empty() ? 0 : dangling.batches.front().barriers.size());
        for (const auto& batch : dangling.batches)
            for (const auto& barrier : batch.barriers)
                ENGINE_LOG_INFO("[Dangling]   release q{}->q{} texture '{}': {} -> {} (for '{}'){}",
                    barrier.source_queue, barrier.target_queue,
                    barrier.resource->Name().c_str(),
                    resource_state_name(barrier.before_state),
                    resource_state_name(barrier.after_state),
                    barrier.target_pass != nullptr ? barrier.target_pass->Name().c_str() : "?",
                    barrier.is_subresource ? " [sub]" : "");
    }
}

void BarrierGenerationPhase::generate_transition_barriers(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{
    // 按全局执行序（=HEFT调度序）遍历——依赖保序的全局序列，队列调度/重排都不会违反它
    const auto& topo_order = queue_schedule_.get_schedule_result().schedule_order;

    for (RDGPassNodeRef pass : topo_order)
    {
        if (!pass || pass->isCulled) continue;

        const PassResourceInfo* res_info = pass_info_analysis_.get_resource_info(pass);
        if (!res_info) continue;

        const uint32_t target_queue = queue_schedule_.get_pass_queue_index(pass);

        // 同步节点推导（wait聚合+signal分配）——先于屏障阶段
        result_.pass_sync_nodes[pass] = build_sync_node(pass, res_info, target_queue, executor);

        for (const auto& access : res_info->resource_accesses)
            process_access(pass, access, target_queue);

        // signal已定，按read/write推进tracker
        advance_sync_trackers(res_info, target_queue, result_.pass_sync_nodes[pass].signal.second, executor);
    }
}

void BarrierGenerationPhase::process_access(RDGPassNodeRef pass, const ResourceAccessInfo& access, uint32_t target_queue)
{
    if (access.resource == nullptr) return;

    const uint32_t target_family = queue_schedule_.get_queue_family_index(target_queue); 

    if (access.resource->NodeType() == RDG_RESOURCE_NODE_TYPE_TEXTURE)
    {
        auto texture = static_cast<RDGTextureNodeRef>(access.resource);
        auto found = texture_trackers_.find(texture);
        if (found == texture_trackers_.end()) return;
        TextureStateTracker& tracker = found->second;

        // 解析访问范围（0 = 默认，覆盖整个资源）——tracker写回用
        const uint32_t mip_base = access.mip_base;
        const uint32_t mip_count = access.mip_count;    // Analysis展开站已具体化，下游直读
        const uint32_t array_base = access.array_base;
        const uint32_t array_count = access.array_count;

        // ===== ①发射上下文（本次access的循环不变量：队列/族判定）=====
        const uint32_t target_family = queue_schedule_.get_queue_family_index(target_queue);
        const uint32_t source_queue = tracker.last_pass != nullptr
            ? queue_schedule_.get_pass_queue_index(tracker.last_pass) : target_queue;
        // 族归属：直接读所有权追踪（帧首=池族/imported族初值；帧内=最近转移后的现任族）。
        // 与旧"由last_pass队列推导"等价（见TextureStateTracker::owner_family注释）
        const uint32_t source_family = tracker.owner_family;
        const bool cross_queue = tracker.last_pass != nullptr && source_queue != target_queue;
        const bool cross_family = source_family != RHI_QUEUE_FAMILY_IGNORED && source_family != target_family;
        // 帧首跨族
        const bool frame_start_cross_family = tracker.last_pass == nullptr && cross_family;

        // 跨队列异族：EXCLUSIVE纹理须由生产者队列release + 消费者队列acquire完成所有权转移。
        //（CONCURRENT纹理在此短路：owner恒IGNORED下cross_family已恒假，此条件为防御性显式门——
        //  防未来owner泄漏路径对CONCURRENT图像发族对屏障，validation层会判非法）
        const bool qfo = (cross_queue || frame_start_cross_family) && cross_family &&
            texture->info.sharingMode != RESOURCE_SHARING_TYPE_CONCURRENT;

        // ===== ②范围内(state,frequency)等价类分组（发射最小单位）=====
        const std::vector<TextureSubrangeGroup> groups = compute_texture_subrange_groups(tracker, access);
        assert(access.aspect != TEXTURE_ASPECT_NONE);

        // ===== ③逐组发射 =====
        for (const TextureSubrangeGroup& group : groups)
        {
            // 组级屏障需求：状态变化，或UAV→UAV同态写后读可见性
            const bool need = group.state != access.resource_state ||
                (group.state == RESOURCE_STATE_UNORDERED_ACCESS && access.resource_state == RESOURCE_STATE_UNORDERED_ACCESS && !cross_queue);
            if (!need && !qfo) continue;

            for (const TextureSubresourceRange& range : group.ranges)
            {
                TextureSubresourceRange sub = range;
                sub.aspect = access.aspect;
                const bool is_subresource = !(sub.baseMipLevel == 0 && sub.levelCount == tracker.mip_levels &&
                                              sub.baseArrayLayer == 0 && sub.layerCount == tracker.array_layers);

                if (qfo)
                {
                    // 转移在acquire侧：release=纯所有权转移
                    // 逐组发射：每组携带自己的before态，混合范围不再被首子资源状态覆盖——
                    // QFO配对约束acquire.old==release.new由两侧同取group.state构造保证
                    if (tracker.last_pass != nullptr)
                        emit_barrier(result_.pass_sync_nodes[tracker.last_pass], RDGBarrier{
                            .resource = texture,
                            .source_pass = tracker.last_pass,
                            .target_pass = pass,
                            .source_queue = source_queue,
                            .target_queue = target_queue,
                            .src_family = source_family,
                            .dst_family = target_family,
                            .before_state = group.state,
                            .after_state = group.state,     // 纯所有权转移，布局不动（转移在acquire侧）
                            .src_frequency = group.frequency,
                            .dst_frequency = group.frequency,   // release侧无新shader使用：dst镜像src
                            .after_pass = true,     // release挂生产者pass之后
                            .is_subresource = is_subresource,
                            .subresource = sub,
                        });

                    emit_barrier(result_.pass_sync_nodes[pass], RDGBarrier{
                        .resource = texture,
                        .source_pass = tracker.last_pass,    // 帧首跨族时nullptr（所有权来自owner_family初值/悬空release）
                        .target_pass = pass,
                        .source_queue = source_queue,
                        .target_queue = target_queue,
                        .src_family = source_family,
                        .dst_family = target_family,
                        .before_state = group.state,             // ==release.new（QFO配对约束）
                        .after_state = access.resource_state,    // 真实布局转移在消费队列完成
                        .src_frequency = group.frequency,        // 帧首跨族时账本=ALL（上帧末使用不入registry）
                        .dst_frequency = access.frequency,
                        .is_subresource = is_subresource,
                        .subresource = sub,
                    });
                }
                else if (need)
                {
                    // 同队列：状态变化或UAV→UAV（写后读可见性）时发转移屏障；
                    // 跨队列同族：timeline信号量wait已建立完整执行+内存依赖，仅状态变化时需要布局转移屏障
                    emit_barrier(result_.pass_sync_nodes[pass], RDGBarrier{
                        .resource = texture,
                        .source_pass = tracker.last_pass,
                        .target_pass = pass,
                        .source_queue = source_queue,
                        .target_queue = target_queue,
                        .before_state = group.state,
                        .after_state = access.resource_state,
                        .src_frequency = group.frequency,    // S6账本组内单值
                        .dst_frequency = access.frequency,
                        .is_subresource = is_subresource,
                        .subresource = sub,
                    });
                }
            }
        }

        // 所有权随转移对更新为目标族（后续同族访问不再发转移——判定读owner_family而非
        // 由last_pass推导，见tracker注释）
        if (qfo)
            tracker.owner_family = target_family;

        // 更新跟踪器
        // 频率账本：同pass多次access（如同一资源vert+frag各一槽）取并集；跨pass赋值——
        // 前一状态时代的频率已被本次屏障消费（或无需屏障：read-read同态），新状态从本次access重新记账。
        // 帧首触达赋值丢弃跨帧ALL是安全的：跨帧由槽fence/QFO wait建立依赖，屏障只管帧内
        const bool merge_frequency = tracker.last_pass == pass;
        for (uint32_t m = mip_base; m < mip_base + mip_count; m++)
            for (uint32_t l = array_base; l < array_base + array_count; l++)
            {
                tracker.states[tracker.index(m, l)] = access.resource_state;
                tracker.state_frequencies[tracker.index(m, l)] =
                    merge_frequency ? (tracker.state_frequencies[tracker.index(m, l)] | access.frequency)
                                    : access.frequency;
            }
        tracker.last_pass = pass;
        if (tracker.owner_family == RHI_QUEUE_FAMILY_IGNORED && texture->info.sharingMode != RESOURCE_SHARING_TYPE_CONCURRENT)
            tracker.owner_family = target_family;   // 初值IGNORED（未指定所有权，如新池条目）：
                                                    // 首触采纳本族为基线——与旧"由last_pass队列推导"
    }
    else
    {
        RDGBufferNodeRef buffer = static_cast<RDGBufferNodeRef>(access.resource);
        auto found = buffer_trackers_.find(buffer);
        if (found == buffer_trackers_.end())return;

        BufferStateTracker& tracker = found->second;

        const RHIResourceState before = tracker.state;
        const uint32_t source_queue = tracker.last_pass != nullptr
            ? queue_schedule_.get_pass_queue_index(tracker.last_pass) : target_queue;
        const bool cross_queue = tracker.last_pass != nullptr && source_queue != target_queue;

        // buffer不做队列族所有权转移（族恒IGNORED）：跨队列执行/内存同步由timeline信号量覆盖，
        // 仅状态变化时需要转移屏障；写后写（WAW）同态额外需要可见性屏障——UAV→UAV（帧内写后读）
        // 与AS→AS（跨帧imported：TLAS storage每帧build，sync validation实证帧间WAW缺口）
        const bool stateChange = before != access.resource_state;
        const bool wawSameState =
            (before == RESOURCE_STATE_UNORDERED_ACCESS || before == RESOURCE_STATE_ACCELERATION_STRUCTURE) &&
            before == access.resource_state;
        const bool need = stateChange || wawSameState;

        if (need)
        {
            // 族恒IGNORED/after_pass=false/type=Transition均走默认值（聚合初始化省略）
            emit_barrier(result_.pass_sync_nodes[pass], RDGBarrier{
                .resource = buffer,
                .source_pass = tracker.last_pass,
                .target_pass = pass,
                .source_queue = source_queue,
                .target_queue = target_queue,
                .before_state = before,
                .after_state = access.resource_state,
                .src_frequency = tracker.state_frequency,
                .dst_frequency = access.frequency,
                .buffer_offset = static_cast<uint32_t>(access.buffer_from),
                .buffer_size = static_cast<uint32_t>(access.buffer_to - access.buffer_from),
            });
        }

        tracker.state = access.resource_state;
        tracker.state_frequency = tracker.last_pass == pass
            ? (tracker.state_frequency | access.frequency)   // 同pass多access取并集（与纹理账本同规则）
            : access.frequency;
        tracker.last_pass = pass;
    }
}

// 族→队列反查（族:队列=1:1前提下的代表队列——悬空起点的挂靠队列）
static uint32_t family_queue_index(const QueueSchedule& schedule, uint32_t family)
{
    for (const QueueInfo& qi : schedule.get_schedule_result().all_queues)
        if (qi.family_index == family)
            return qi.index;
    return 0;   // 启用族必有队列，不可达
}

void BarrierGenerationPhase::prebuild_dangling_starts(PerFrameCommonResourceRef executor)
{
    // 沿全局调度序线性扫（与走查同过滤：isCulled），逐纹理定格首触access——悬空的存在性/成员/
    // signal值/waits/帧首release/+1落账全部在此定格，走查首触特判整删。buffer无悬空不扫。
    // 判定（与旧首触特判同源三分支）：种子族（import+registry命中=记录owner族 / import未命中
    // =graphics / 池化=池条目initFamily）≠ 首触者族 && EXCLUSIVE → 该族聚合虚拟节点。
    // CONCURRENT显式排除：设计上owner恒IGNORED免QFO，防registry记录的sync_queue带回族域
    const std::vector<RDGPassNodeRef>& topo_order = queue_schedule_.get_schedule_result().schedule_order;
    std::unordered_set<RDGTextureNodeRef> firstSeen;

    for (RDGPassNodeRef pass : topo_order)
    {
        if (!pass || pass->isCulled) continue;
        const PassResourceInfo* res_info = pass_info_analysis_.get_resource_info(pass);
        if (!res_info) continue;

        const uint32_t target_queue = queue_schedule_.get_pass_queue_index(pass);
        const uint32_t target_family = queue_schedule_.get_queue_family_index(target_queue);

        for (const ResourceAccessInfo& access : res_info->resource_accesses)
        {
            if (access.resource == nullptr) continue;
            if (access.resource->NodeType() != RDG_RESOURCE_NODE_TYPE_TEXTURE) continue;
            RDGTextureNodeRef texture = static_cast<RDGTextureNodeRef>(access.resource);
            if (!firstSeen.insert(texture).second) continue;    // 只看首触access（向量序=旧迭代序）

            auto found = texture_trackers_.find(texture);
            if (found == texture_trackers_.end()) continue;
            TextureStateTracker& tracker = found->second;

            uint32_t seed_family;
            uint32_t owner_queue;
            if (tracker.sync_queue != UINT32_MAX)      // import且registry命中：种子=上帧末触族
            {
                seed_family = queue_schedule_.get_queue_family_index(tracker.sync_queue);
                owner_queue = tracker.sync_queue;
            }
            else
            {
                // 其余一律问tracker初值——on_execute的init三目已保证三类资源都有意义：
                // import未命中=graphics静止（"静止graphics"约定的唯一权威在init处，进程首帧史
                // =引擎初始化全在gfx队列）/池化=initFamily；CONCURRENT与全新池条目=IGNORED
                // →下方短路不悬空。family_queue_index对IGNORED安全（回落0后被短路）
                seed_family = tracker.owner_family;
                owner_queue = family_queue_index(queue_schedule_, seed_family);
            }

            if (seed_family == RHI_QUEUE_FAMILY_IGNORED || seed_family == target_family) continue;
            if (texture->info.sharingMode == RESOURCE_SHARING_TYPE_CONCURRENT) continue;

            RDGSyncNode& dangling = result_.dangling_starts[seed_family];
            if (dangling.signal.first == nullptr)  // 该族首个悬空：signal预分配（此后本帧一切pass值必大于它）
            {
                RHISemaphoreRef owner_timeline = executor->queueSlots[owner_queue].timeline;
                dangling.signal.first = owner_timeline;
                dangling.signal.second = cross_frame_registry_.alloc_timeline_signal(owner_timeline.get(), 0);
            }
            // 并入waits：资源seed记录逐线取max(same,cross)
            for (const auto& [timeline, pair] : tracker.sync_signals)
            {
                // 悬空起点需要等待这个资源在这个队列时间线上的同族singal和异族singal的最大值
                const uint64_t value = std::max(pair.first, pair.second);
                
                if (auto it = std::ranges::find_if(dangling.waits, [timeline](const auto& wait) {return wait.timeline == timeline; }); it != dangling.waits.end())
                    it->value = std::max(it->value, value);
                else
                    dangling.waits.push_back({ timeline, value, {} });
            }
            // patch资源所在队列时间线的异族值——异族首触者经fold_record取到它；同族后续访问取同族值不等它（read语义）
            tracker.sync_signals[dangling.signal.first].second = dangling.signal.second;
            tracker.sync_queue = owner_queue;      // 悬空触碰：挂靠队列即资源所在队列

            // 帧首release屏障（QFO转移，无布局变化）：挂悬空节点batches，Phase 8录进该族队列
            // 流首prologue批发射。范围=首触access逐组（与process_access帧首acquire按同一
            // 分组输出同range——QFO配对要求范围一致；首触前tracker未被本帧改写，两侧输入
            // 恒同）。逐组发射：旧形态取首子资源state做整范围(F,F)转移，混合范围oldLayout错位。纯所有权(F,F)：F=组内tracker现态
            //（import=registry记录/池化=池记终态），布局转移在配对acquire侧完成。
            // src/dst_frequency保持默认ALL：src=上帧末使用频率（不入registry，查不到=保守ALL）；
            // dst=纯所有权转移无新shader使用。配对acquire在process_access带真实dst频率
            assert(access.aspect != TEXTURE_ASPECT_NONE);
            for (const TextureSubrangeGroup& group : compute_texture_subrange_groups(tracker, access))
                for (const TextureSubresourceRange& range : group.ranges)
                {
                    TextureSubresourceRange sub = range;
                    sub.aspect = access.aspect;
                    emit_barrier(dangling, RDGBarrier{
                        .resource = texture,
                        .source_pass = nullptr,         // 悬空起点=帧首虚拟节点，无本帧生产者
                        .target_pass = pass,             // 首触消费者（日志锚定）
                        .source_queue = owner_queue,
                        .target_queue = target_queue,
                        .src_family = seed_family,
                        .dst_family = target_family,
                        .before_state = group.state,
                        .after_state = group.state,     // 纯所有权转移，布局不动
                        .is_subresource = !(sub.baseMipLevel == 0 && sub.levelCount == tracker.mip_levels &&
                            sub.baseArrayLayer == 0 && sub.layerCount == tracker.array_layers),
                        .subresource = sub,
                    });
                }
        }
    }
}

RDGSyncNode BarrierGenerationPhase::build_sync_node(RDGPassNodeRef pass, const PassResourceInfo* res_info, uint32_t target_queue, PerFrameCommonResourceRef executor)
{
    const uint32_t target_family = queue_schedule_.get_queue_family_index(target_queue);
    const RHISemaphoreRef& target_timeline = executor->queueSlots[target_queue].timeline;

    // wait聚合表：时间线→包含集（逐线max）。键持ref顺带跨帧保活旧时间线对象
    std::unordered_map<RHISemaphoreRef, RDGSyncNode::WaitEntry> waits;

    auto fold_record = [&](const std::unordered_map<RHISemaphoreRef, std::pair<uint64_t, uint64_t>>& signals,
        uint32_t resource_family, RHIResourceState accessState, uint32_t accessFrequency)
        {
            // CONCURRENT（族IGNORED）无所有权概念：按"异族语义"取cross——read也推进该值，跨队列
            // WAR（读→写）由此覆盖；write推双值天然覆盖RAW/WAW。read-read跨队列会多等一次
            // （过同步，无害）。EXCLUSIVE正常走同族/异族选值（同族值=最后write，同队列冲突由
            // 状态屏障覆盖；异族值=最后任意访问，跨队列全冲突类型覆盖）
            const bool same_family = resource_family != RHI_QUEUE_FAMILY_IGNORED && resource_family == target_family;
            for (const auto& [timeline, pair] : signals)
            {
                RDGSyncNode::WaitEntry& entry = waits[timeline];
                entry.timeline = timeline;
                entry.value = std::max(entry.value, same_family ? pair.first : pair.second);

                // 窄化账目：贡献者(state,frequency)对——同state按位OR frequency（推导线性=合并精确），
                // 后端逐对展开stage取并。state/frequency取自本access的现成单值
                if(auto it = std::ranges::find_if(entry.usages, [accessState](const auto& usage) { return usage.first == accessState; }); it != entry.usages.end())
					it->second |= accessFrequency;
				else
					entry.usages.push_back({ accessState, accessFrequency });
            }
        };

    for (const auto& access : res_info->resource_accesses)
    {
        if (access.resource == nullptr) continue;

        if (access.resource->NodeType() == RDG_RESOURCE_NODE_TYPE_TEXTURE)
        {
            auto found = texture_trackers_.find(static_cast<RDGTextureNodeRef>(access.resource));
            if (found == texture_trackers_.end()) continue;
            TextureStateTracker& tracker = found->second;

            // 悬空起点与+1落账已由prebuild_dangling_starts在走查前定格——异族首触者的悬空等待已在种子sync_signals里，fold直接取到

            // 资源族=访问前现任族（owner_family由屏障逻辑维护：跨族acquire后才改写为目标族——
            // 本pass等的是转移前所有者留下的值）
            fold_record(tracker.sync_signals, tracker.owner_family, access.resource_state, access.frequency);

            // QFO acquire的生产者侧账目:跨族访问将发acquire屏障,其转移写本身
            // 是"src侧阶段的工作"——若wait掩码只含消费侧shader阶段,转移与异族生产者附件写并发
            // (syncval 25条WRITE_AFTER_WRITE实锤:PointShadow Filter的COLOR→UAV转移 vs q0渲染)。
            // 逐组贡献(state,frequency)（发射重构升级:旧取首子资源单值,混合范围漏组;
            // fold内同state自动OR合并——推导对frequency线性,合并精确）
            if (tracker.owner_family != RHI_QUEUE_FAMILY_IGNORED && tracker.owner_family != target_family &&
                static_cast<RDGTextureNodeRef>(access.resource)->info.sharingMode != RESOURCE_SHARING_TYPE_CONCURRENT)
            {
                for (const TextureSubrangeGroup& group : compute_texture_subrange_groups(tracker, access))
                    fold_record(tracker.sync_signals, tracker.owner_family, group.state, group.frequency);
            }
        }
        else
        {
            auto found = buffer_trackers_.find(static_cast<RDGBufferNodeRef>(access.resource));
            if (found == buffer_trackers_.end()) continue;
            BufferStateTracker& tracker = found->second;
            // buffer无所有权概念→无悬空起点
            // 资源族跟随末触pass（sync_queue→族；未触=空记录，族值不影响fold）
            fold_record(tracker.sync_signals, tracker.sync_queue == UINT32_MAX
                ? RHI_QUEUE_FAMILY_IGNORED
                : queue_schedule_.get_queue_family_index(tracker.sync_queue),access.resource_state, access.frequency);
        }
    }

    RDGSyncNode node;
    // signal = wait+1（实现=max(wait[本线], B[本线])+1并回写B——B管跨帧值序与空wait兜底，规则①）
    node.signal.first = target_timeline;
    node.signal.second = cross_frame_registry_.alloc_timeline_signal(target_timeline.get(), waits[target_timeline].value);
    node.waits.reserve(waits.size());
    for (auto& [timeline, entry] : waits)
        node.waits.push_back(std::move(entry));
    return node;
}

void BarrierGenerationPhase::advance_sync_trackers(const PassResourceInfo* res_info, uint32_t target_queue, uint64_t signal, PerFrameCommonResourceRef executor)
{
    const RHISemaphoreRef timeline = executor->queueSlots[target_queue].timeline;

    for (const auto& access : res_info->resource_accesses)
    {
        if (access.resource == nullptr) continue;
        // ReadWrite按write处理（保守正确：双值都推只过同步不漏同步）
        const bool write = access.access_type != EResourceAccessType::Read;

        if (access.resource->NodeType() == RDG_RESOURCE_NODE_TYPE_TEXTURE)
        {
            auto found = texture_trackers_.find(static_cast<RDGTextureNodeRef>(access.resource));
            if (found == texture_trackers_.end()) continue;
            TextureStateTracker& tracker = found->second;
            // 落在pass所在队列时间线上：read只推异族、write推双值。
            // 同pass多access逐条覆盖，终值=最后一条的访问类型
            auto& slot = tracker.sync_signals[timeline];
            slot.second = signal;
            if (write) slot.first = signal;
            tracker.sync_queue = target_queue;
        }
        else
        {
            auto found = buffer_trackers_.find(static_cast<RDGBufferNodeRef>(access.resource));
            if (found == buffer_trackers_.end()) continue;
            BufferStateTracker& tracker = found->second;
            auto& slot = tracker.sync_signals[timeline];
            slot.second = signal;
            if (write) slot.first = signal;
            tracker.sync_queue = target_queue;
        }
    }
}

void BarrierGenerationPhase::write_back_sync_records(RDGDependencyGraphRef graph)
{
    // 未触（last_pass空）/池化不写——registry merge换代保旧记录（族/布局信息跨帧有效）。
    for (auto& [node, tracker] : texture_trackers_)
    {
        if (tracker.last_pass == nullptr || !node->IsImported()) continue;
        const void* key = binding_phase_.get_texture(node).get();
        if (key == nullptr) continue;   // 防御：import绑定空（正常不可达）

        RDGCrossFrameRegistry::ResourceSyncRecord record;
        record.owner_queue = tracker.sync_queue;
        record.signals = tracker.sync_signals;   // 含旧时间线记录：下帧wait=同槽fence语义瞬过，无害
        record.final_states = tracker.states;    // 布局动态记载：真实终态（下帧种子权威）
        cross_frame_registry_.record_resource_sync(key, std::move(record));
    }
    for (auto& [node, tracker] : buffer_trackers_)
    {
        if (tracker.last_pass == nullptr || !node->IsImported()) continue;
        const void* key = binding_phase_.get_buffer(node).get();
        if (key == nullptr) continue;

        RDGCrossFrameRegistry::ResourceSyncRecord record;
        record.owner_queue = tracker.sync_queue;
        record.signals = tracker.sync_signals;
        cross_frame_registry_.record_resource_sync(key, std::move(record));
    }
}
