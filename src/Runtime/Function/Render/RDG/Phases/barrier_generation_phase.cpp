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
}

void BarrierGenerationPhase::on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{
    ENGINE_TIME_SCOPE(BarrierGenerationPhase::on_execute);

    // 初始化状态跟踪器：
    //   imported → Build期声明的 initState
    //   created  → 池化跨帧状态（阶段6集中分配时由池条目写入 node->initState）
    graph->ForEachTextureNode([&](RDGTextureNodeRef texture) {
        TextureStateTracker tracker;
        assert(texture->info.mipLevels > 0 && texture->info.arrayLayers > 0);
        tracker.mip_levels = texture->info.mipLevels;
        tracker.array_layers = texture->info.arrayLayers;
        tracker.states.assign(static_cast<size_t>(tracker.mip_levels) * tracker.array_layers, texture->initState);
        texture_trackers_[texture] = std::move(tracker);
    });

    graph->ForEachBufferNode([&](RDGBufferNodeRef buffer) {
        BufferStateTracker tracker;
        tracker.last_pass = nullptr;
        tracker.state = buffer->initState;
        buffer_trackers_[buffer] = std::move(tracker);
    });

    generate_transition_barriers(graph);

    if (config_.enable_debug_output)
        debug_info();

}

const std::vector<BarrierBatch>* BarrierGenerationPhase::get_pass_barrier_batches(RDGPassNodeRef pass) const
{
    if (auto found = result_.pass_barrier_batches.find(pass); found != result_.pass_barrier_batches.end())
        return &found->second;
    return nullptr;
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
        bool need = false;
        for (uint32_t m = mip_base; m < mip_base + mip_count && !need; m++)
        {
            for (uint32_t l = array_base; l < array_base + array_count; l++)
            {
                const RHIResourceState current = tracker.states[tracker.index(m, l)];
                if (current != access.resource_state) { need = true; break; }
                if (current == RESOURCE_STATE_UNORDERED_ACCESS &&
                    access.resource_state == RESOURCE_STATE_UNORDERED_ACCESS) { need = true; break; }  // UAV→UAV 同状态也需要写后读可见性
            }
        }
        // 跨队列访问：即使状态相同也要建立依赖（多队列提交期的同步需求，单流录制下为无害屏障）
        if (!need && tracker.last_pass != nullptr &&
            sync_analysis_.get_pass_queue_index(tracker.last_pass) != target_queue)
            need = true;

        if (need)
        {
            RDGBarrier barrier{};
            barrier.resource = texture;
            barrier.type = EBarrierType::ResourceTransition;
            barrier.source_pass = tracker.last_pass;
            barrier.target_pass = pass;
            barrier.source_queue = tracker.last_pass != nullptr ? sync_analysis_.get_pass_queue_index(tracker.last_pass) : target_queue;
            barrier.target_queue = target_queue;
            barrier.before_state = before;
            barrier.after_state = access.resource_state;
            barrier.after_pass = after_pass;
            barrier.is_subresource = !(mip_base == 0 && mip_count == tracker.mip_levels &&
                                       array_base == 0 && array_count == tracker.array_layers);
            barrier.subresource = {
                .aspect = TEXTURE_ASPECT_NONE,
                .baseMipLevel = mip_base,
                .levelCount = mip_count,
                .baseArrayLayer = array_base,
                .layerCount = array_count };

            auto& batches = result_.pass_barrier_batches[pass];
            if (batches.empty()) batches.push_back({{}, EBarrierType::ResourceTransition});
            batches.front().barriers.push_back(barrier);
            result_.total_barriers++;
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
        bool need = before != access.resource_state
                 || (before == RESOURCE_STATE_UNORDERED_ACCESS && access.resource_state == RESOURCE_STATE_UNORDERED_ACCESS)
                 || (tracker.last_pass != nullptr && sync_analysis_.get_pass_queue_index(tracker.last_pass) != target_queue);

        if (need)
        {
            RDGBarrier barrier{};
            barrier.resource = buffer;
            barrier.type = EBarrierType::ResourceTransition;
            barrier.source_pass = tracker.last_pass;
            barrier.target_pass = pass;
            barrier.source_queue = tracker.last_pass != nullptr ? sync_analysis_.get_pass_queue_index(tracker.last_pass) : target_queue;
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
