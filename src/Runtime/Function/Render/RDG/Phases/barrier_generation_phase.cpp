#include "barrier_generation_phase.h"

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

BarrierGenerationPhase::BarrierGenerationPhase(
    const CrossQueueSyncAnalysis& sync_analysis,
    const PassBindingPhase& binding_phase,
    const PassInfoAnalysis& pass_info_analysis,
    const ExecutionReorderPhase& reorder_phase,
    const BarrierGenerationConfig& config)
    : config_(config)
    , sync_analysis_(sync_analysis)
    , binding_phase_(binding_phase)
    , pass_info_analysis_(pass_info_analysis)
    , reorder_phase_(reorder_phase)
{
}

void BarrierGenerationPhase::reset_for_frame()
{
    result_.pass_barrier_batches.clear();
    result_.total_barriers = 0;

    texture_trackers_.clear();
    buffer_trackers_.clear();
    frame_start_releases_.clear();
}

void BarrierGenerationPhase::on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{
    ENGINE_TIME_SCOPE(BarrierGenerationPhase::on_execute);

    // 帧首跨族acquire的族归属基准：all_queues[0]恒为graphics（QueryConfiguredQueues顺序）
    const uint32_t graphicsFamily = sync_analysis_.get_queue_family_index(0);

    // 初始化状态跟踪器：
    //   imported → Build期声明的 initState
    //   created  → 池化跨帧状态（阶段6集中分配时由池条目写入 node->initState）
    //   init_family → 跨帧归属族（created=阶段6写入；imported=graphics族——Builder的Import
    //                不设族，节点initFamily恒为IGNORED，若沿用则帧首跨族判定永不触发：上帧末
    //                留在异族的imported图（如copy队列做完Depth Copy），本帧graphics首用时不发
    //                acquire、布局转移落在错误的族上——validation布局失配的来源。与池化资源
    //                同语义：帧间静止归属恒为graphics族，配合下方帧末归巢）
    graph->ForEachTextureNode([&](RDGTextureNodeRef texture) {
        TextureStateTracker tracker;
        assert(texture->info.mipLevels > 0 && texture->info.arrayLayers > 0);
        tracker.mip_levels = texture->info.mipLevels;
        tracker.array_layers = texture->info.arrayLayers;
        tracker.states.assign(static_cast<size_t>(tracker.mip_levels) * tracker.array_layers, texture->initState);
        tracker.init_family = texture->IsImported() ? graphicsFamily : texture->initFamily;
        texture_trackers_[texture] = std::move(tracker);
    });

    graph->ForEachBufferNode([&](RDGBufferNodeRef buffer) {
        BufferStateTracker tracker;
        tracker.last_pass = nullptr;
        tracker.state = buffer->initState;
        buffer_trackers_[buffer] = std::move(tracker);
    });

    frame_start_releases_.clear();
    generate_transition_barriers(graph);

    // 帧末归巢：本帧最终归属族≠graphics族的纹理（含imported——深度图等持久资源同样会被copy
    // 队列带离graphics族），向最后一个触碰pass的after批次追加 release(F→graphics)。帧槽fence
    // 保证下一帧开始前该release已执行——资源静止归属恒为graphics族（池条目由Phase 8归还时记录；
    // imported由tracker.init_family=graphics承接）。不归巢的帧首异族acquire无法配对release
    // （上一帧不知道下一帧的首用族），prologue机制见get_frame_start_releases
    for (auto& [texture, tracker] : texture_trackers_)
    {
        if (tracker.last_pass == nullptr) continue;

        const uint32_t lastFamily = sync_analysis_.get_queue_family_index(sync_analysis_.get_pass_queue_index(tracker.last_pass));
        if (lastFamily == graphicsFamily || lastFamily == RHI_QUEUE_FAMILY_IGNORED) continue;

        RDGBarrier home{};
        home.resource = texture;
        home.type = EBarrierType::ResourceTransition;
        home.source_pass = tracker.last_pass;
        home.target_pass = nullptr;            // 归巢目标无本帧pass（下一帧的prologue/首用者承接）
        home.source_queue = sync_analysis_.get_pass_queue_index(tracker.last_pass);
        home.target_queue = 0;
        home.src_family = lastFamily;
        home.dst_family = graphicsFamily;
        home.before_state = tracker.states[tracker.index(0, 0)];
        home.after_state = tracker.states[tracker.index(0, 0)];   // 纯所有权转移，无布局变化
        home.after_pass = true;

        auto& batches = result_.pass_barrier_batches[tracker.last_pass];
        if (batches.empty()) batches.push_back({{}, EBarrierType::ResourceTransition});
        batches.front().barriers.push_back(home);
        result_.total_barriers++;
    }

    if (config_.enable_debug_output)
        debug_info();

}

const std::vector<BarrierBatch>* BarrierGenerationPhase::get_pass_barrier_batches(RDGPassNodeRef pass) const
{
    if (auto found = result_.pass_barrier_batches.find(pass); found != result_.pass_barrier_batches.end())
        return &found->second;
    return nullptr;
}

RHIResourceState BarrierGenerationPhase::get_final_texture_state(RDGTextureNodeRef texture) const
{
    if (auto found = texture_trackers_.find(texture); found != texture_trackers_.end())
        return found->second.states.empty() ? RESOURCE_STATE_UNDEFINED : found->second.states.front();
    return RESOURCE_STATE_UNDEFINED;
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
    ENGINE_LOG_INFO("[Barrier] total barriers: {}, passes with batches: {}",
        result_.total_barriers, result_.pass_barrier_batches.size());

    // ===== 逐屏障明细 =====
    // 按拓扑序（即录制/发射顺序）输出，帧间可比；[before]/[after] 与pass命令的相对位置一致
    // [before] = pass之前发射（非output边，pass期间状态）
    // [after]  = pass之后发射（output边，产出收敛状态）
    // (from X) = 上一次触碰该资源的pass（init表示来自资源的初始状态）
    // qA->qB   = source_queue -> target_queue（跨队列时相同状态也会生成屏障）
    const auto& topo_order = sync_analysis_.get_dependency_analysis().get_topological_order();
    for (RDGPassNodeRef pass : topo_order)
    {
        auto found = result_.pass_barrier_batches.find(pass);
        if (found == result_.pass_barrier_batches.end()) continue;

        for (const auto& batch : found->second)
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
}

void BarrierGenerationPhase::generate_transition_barriers(RDGDependencyGraphRef graph)
{
    // 按 Phase 2 拓扑序遍历（依赖保序的全局序列；队列调度/重排都不会违反它）
    const auto& topo_order = sync_analysis_.get_dependency_analysis().get_topological_order();

    for (RDGPassNodeRef pass : topo_order)
    {
        if (!pass || pass->isCulled) continue;

        const PassResourceInfo* res_info = pass_info_analysis_.get_resource_info(pass);
        if (!res_info) continue;

        const uint32_t target_queue = sync_analysis_.get_pass_queue_index(pass);

        // 先处理非output访问（状态是pass期间状态，屏障在pass之前发射），
        // 再处理output访问（状态是pass之后的收敛状态，屏障在pass之后发射），
        // 与旧路径 CreateInputBarriers → pass → CreateOutputBarriers 的时序一致
        for (int phase = 0; phase < 2; phase++)
        {
            const bool output_phase = (phase == 1);
            for (const auto& access : res_info->resource_accesses)
            {
                if (access.is_output != output_phase) continue;
                process_access(pass, access, target_queue, output_phase);
            }
        }
    }
}

void BarrierGenerationPhase::process_access(RDGPassNodeRef pass, const ResourceAccessInfo& access, uint32_t target_queue, bool after_pass)
{
    if (access.resource == nullptr) return;

    const uint32_t target_family = sync_analysis_.get_queue_family_index(target_queue);

    if (access.resource->NodeType() == RDG_RESOURCE_NODE_TYPE_TEXTURE)
    {
        auto texture = static_cast<RDGTextureNodeRef>(access.resource);
        auto found = texture_trackers_.find(texture);
        if (found == texture_trackers_.end()) return;
        TextureStateTracker& tracker = found->second;

        // 解析访问范围（0 = 默认，覆盖整个资源）
        const uint32_t mip_base = access.mip_base;
        const uint32_t mip_count = access.mip_count == 0 ? tracker.mip_levels : access.mip_count;
        const uint32_t array_base = access.array_base;
        const uint32_t array_count = access.array_count == 0 ? tracker.array_layers : access.array_count;

        // 判定是否需要屏障（假定范围内各子资源状态一致，取首个子资源的状态作为before；
        // 与旧 PreviousState 的启发式一致——默认范围只追踪最近状态，子范围要求精确匹配）
        const RHIResourceState before = tracker.states[tracker.index(mip_base, array_base)];
        bool stateChange = false;
        bool uavToUav = false;
        for (uint32_t m = mip_base; m < mip_base + mip_count && !(stateChange || uavToUav); m++)
        {
            for (uint32_t l = array_base; l < array_base + array_count; l++)
            {
                const RHIResourceState current = tracker.states[tracker.index(m, l)];
                if (current != access.resource_state) { stateChange = true; break; }
                if (current == RESOURCE_STATE_UNORDERED_ACCESS &&
                    access.resource_state == RESOURCE_STATE_UNORDERED_ACCESS) { uavToUav = true; break; }  // UAV→UAV 同状态也需要写后读可见性
            }
        }

        const uint32_t source_queue = tracker.last_pass != nullptr
            ? sync_analysis_.get_pass_queue_index(tracker.last_pass) : target_queue;
        // 族归属：帧内取上次触碰pass的族；帧首（无last_pass）取池族（跨帧所有权，Phase 6写入）
        const uint32_t source_family = tracker.last_pass != nullptr
            ? sync_analysis_.get_queue_family_index(source_queue) : tracker.init_family;
        const bool cross_queue = tracker.last_pass != nullptr && source_queue != target_queue;
        const bool cross_family = source_family != RHI_QUEUE_FAMILY_IGNORED && source_family != target_family;

        const bool is_subresource = !(mip_base == 0 && mip_count == tracker.mip_levels &&
                                      array_base == 0 && array_count == tracker.array_layers);
        const TextureSubresourceRange subresource = {
            .aspect = TEXTURE_ASPECT_NONE,
            .baseMipLevel = mip_base,
            .levelCount = mip_count,
            .baseArrayLayer = array_base,
            .layerCount = array_count };

        // 帧首跨族（池族≠本队列族；归巢机制下池族恒为graphics族）：帧末归巢release已把所有权
        // 交还graphics，帧首首个使用者族G≠graphics时需要 acquire(graphics→G)——配对的
        // release(graphics→G)由Phase 8录进graphics流起点的prologue（受prologueDone信号量边保护）
        const bool frame_start_cross_family = tracker.last_pass == nullptr && cross_family;

        if ((cross_queue || frame_start_cross_family) && cross_family)
        {
            // 跨队列异族：EXCLUSIVE纹理须由生产者队列release + 消费者队列acquire完成所有权转移。
            // release顺带完成布局转移（before→目标态）；acquire侧布局不变，仅收回所有权+可见性。
            // 跨队列的执行/内存同步由同步点的timeline信号量提供（每个跨队列依赖必有SSIS点覆盖）
            if (tracker.last_pass != nullptr)
            {
                RDGBarrier release{};
                release.resource = texture;
                release.type = EBarrierType::ResourceTransition;
                release.source_pass = tracker.last_pass;
                release.target_pass = pass;
                release.source_queue = source_queue;
                release.target_queue = target_queue;
                release.src_family = source_family;
                release.dst_family = target_family;
                release.before_state = before;
                release.after_state = access.resource_state;
                release.after_pass = true;      // release挂生产者pass之后（其output收敛屏障之后）
                release.is_subresource = is_subresource;
                release.subresource = subresource;

                auto& producer_batches = result_.pass_barrier_batches[tracker.last_pass];
                if (producer_batches.empty()) producer_batches.push_back({{}, EBarrierType::ResourceTransition});
                producer_batches.front().barriers.push_back(release);
                result_.total_barriers++;
            }

            RDGBarrier acquire{};
            acquire.resource = texture;
            acquire.type = EBarrierType::ResourceTransition;
            acquire.source_pass = tracker.last_pass;     // 帧首跨族时为nullptr（来自init_family）
            acquire.target_pass = pass;
            acquire.source_queue = source_queue;
            acquire.target_queue = target_queue;
            acquire.src_family = source_family;
            acquire.dst_family = target_family;
            acquire.is_subresource = is_subresource;
            acquire.subresource = subresource;
            acquire.after_pass = false;     // acquire挂消费者pass之前（即使output边访问——消费在本pass）

            if (tracker.last_pass == nullptr)
            {
                // 帧首acquire：布局转移随acquire（上一帧末无本帧release，fence前布局=池化状态）；
                // 并生成配对的prologue release（graphics→G，Phase 8录进graphics流起点）
                acquire.before_state = before;
                acquire.after_state = access.resource_state;

                RDGBarrier prologueRelease = acquire;
                prologueRelease.source_pass = nullptr;
                prologueRelease.target_pass = pass;
                prologueRelease.before_state = access.resource_state;   // release侧无布局变化
                prologueRelease.after_state = access.resource_state;
                frame_start_releases_.push_back(prologueRelease);
            }
            else
            {
                acquire.before_state = access.resource_state;    // 帧内acquire：布局已由release转移到位
                acquire.after_state = access.resource_state;
            }

            auto& consumer_batches = result_.pass_barrier_batches[pass];
            if (consumer_batches.empty()) consumer_batches.push_back({{}, EBarrierType::ResourceTransition});
            consumer_batches.front().barriers.push_back(acquire);
            result_.total_barriers++;
        }
        else
        {
            // 同队列：状态变化或UAV→UAV（写后读可见性）时发转移屏障；
            // 跨队列同族：timeline信号量wait已建立完整执行+内存依赖，仅状态变化时需要布局转移屏障，
            // 同态/UAV→UAV的跨队列可见性由信号量覆盖，不再发冗余屏障
            const bool need = stateChange || (!cross_queue && uavToUav);

            if (need)
            {
                RDGBarrier barrier{};
                barrier.resource = texture;
                barrier.type = EBarrierType::ResourceTransition;
                barrier.source_pass = tracker.last_pass;
                barrier.target_pass = pass;
                barrier.source_queue = source_queue;
                barrier.target_queue = target_queue;
                barrier.before_state = before;
                barrier.after_state = access.resource_state;
                barrier.after_pass = after_pass;
                barrier.is_subresource = is_subresource;
                barrier.subresource = subresource;

                auto& batches = result_.pass_barrier_batches[pass];
                if (batches.empty()) batches.push_back({{}, EBarrierType::ResourceTransition});
                batches.front().barriers.push_back(barrier);
                result_.total_barriers++;
            }
        }

        // 更新跟踪器
        for (uint32_t m = mip_base; m < mip_base + mip_count; m++)
            for (uint32_t l = array_base; l < array_base + array_count; l++)
                tracker.states[tracker.index(m, l)] = access.resource_state;
        tracker.last_pass = pass;
    }
    else
    {
        RDGBufferNodeRef buffer = static_cast<RDGBufferNodeRef>(access.resource);
        auto found = buffer_trackers_.find(buffer);
        if (found == buffer_trackers_.end())return;

        BufferStateTracker& tracker = found->second;

        const RHIResourceState before = tracker.state;
        const uint32_t source_queue = tracker.last_pass != nullptr
            ? sync_analysis_.get_pass_queue_index(tracker.last_pass) : target_queue;
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
            RDGBarrier barrier{};
            barrier.resource = buffer;
            barrier.type = EBarrierType::ResourceTransition;
            barrier.source_pass = tracker.last_pass;
            barrier.target_pass = pass;
            barrier.source_queue = source_queue;
            barrier.target_queue = target_queue;
            barrier.before_state = before;
            barrier.after_state = access.resource_state;
            barrier.after_pass = after_pass;
            barrier.buffer_offset = static_cast<uint32_t>(access.buffer_from);
            barrier.buffer_size = static_cast<uint32_t>(access.buffer_to - access.buffer_from);

            auto& batches = result_.pass_barrier_batches[pass];
            if (batches.empty()) batches.push_back({{}, EBarrierType::ResourceTransition});
            batches.front().barriers.push_back(barrier);
            result_.total_barriers++;
        }

        tracker.state = access.resource_state;
        tracker.last_pass = pass;
    }
}
