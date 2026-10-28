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
    // 物理图双节点守卫：帧内两个RDG节点绑定同一物理纹理即告警（图层病根——曾在tracker层以
    // 物理键共享兜底，2026-10-26病根G-Buffer IOR/Gizmo Data修于图层后共享退役，tracker回归
    // 按节点各开）。池化帧内不复用同一条目（last-use释放统一在录制完成后），故指针重复只会
    // 来自import双节点
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
        // CONCURRENT纹理无队列族所有权概念（双族共享，免QFO往返）：owner恒IGNORED——
        // cross_family判定恒假→不发release/acquire、不归巢，布局转移走else分支（族IGNORED）
        tracker.owner_family = texture->info.sharingMode == RESOURCE_SHARING_TYPE_CONCURRENT
            ? RHI_QUEUE_FAMILY_IGNORED
            : (texture->IsImported() ? graphicsFamily : texture->initFamily);

        // 刀2a新模型seed：import纹理从registry取上帧末同步状态（含旧时间线记录——本帧wait它们
        // =同槽fence语义，已signal瞬间通过；同时持ref跨帧保活时间线对象）。未命中=上帧未触
        // （registry merge换代保旧记录，真未触即进程首见）→空记录，种子族回退graphicsFamily
        if (texture->IsImported())
        {
            // registry键现取不落tracker（刀A）：键/import身份在用点问node与binding_phase
            if (const void* key = binding_phase_.get_texture(texture).get())
            if (const RDGCrossFrameRegistry::ResourceSyncRecord* rec = cross_frame_registry_.find_resource_sync(key))
            {
                tracker.sync_queue = rec->owner_queue;
                tracker.sync_signals = rec->signals;
                // 刀2b：owner_family与种子族必须同源——上帧末触族（旧规则"imported恒graphics"
                // 是归巢制前提，新模型资源可跨帧留在异族）。process_access的跨族acquire判定与
                // build_sync_node的悬空起点判定读同一个族，否则出现acquire无配对release（validation必抓）
                tracker.owner_family = queue_schedule_.get_queue_family_index(rec->owner_queue);
                // 布局动态记载（2026-10-26）：记录的逐子资源真实终态覆盖Import兜底（尺寸同构=
                // 同一张图；声明撒谎病根——末位收敛pass被cull的帧，声明SRV实际COLOR——就此根治）
                if (rec->final_states.size() == tracker.states.size())
                    tracker.states = rec->final_states;
            }
        }
        else
        {
            // 池化种子（悬空承接制 2026-10-26）：本槽各时间线上代末B的{B,B}快照——槽fence已保证
            // 上代全部完成（池按槽隔离，条目只在同槽代际轮回），waits瞬过；种子的实质作用是给
            // 悬空+1提供落账锚点（acquirer据此拿到真实的B+1 wait）与"release排在上代一切使用
            // 之后"的保守自文档语义。布局/族种子来自池条目（initState/initFamily，阶段6写入）
            for (uint32_t q = 0; q < executor->queueSlots.size(); q++)
            {
                const RHISemaphoreRef& timeline = executor->queueSlots[q].timeline;
                const uint64_t b = cross_frame_registry_.peek_timeline_counter(timeline.get());
                tracker.sync_signals[timeline] = { b, b };
            }
        }
    });

    graph->ForEachBufferNode([&](RDGBufferNodeRef buffer) {
        BufferStateTracker tracker;
        tracker.last_pass = nullptr;
        tracker.state = buffer->initState;

        // 刀2a新模型seed：buffer与纹理同构（族跟随末触pass，无悬空/无QFO）
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

    // 刀A：悬空起点预构建——走查前定格（signal低位预分配=值序跟随提交序的构造保证）
    prebuild_dangling_starts(executor, graphicsFamily);

    generate_transition_barriers(graph, executor);

    // 刀C（2026-10-28）：import帧末归巢退役——import与池化同走悬空承接制（真末族经write_back
    // 入registry，下帧首触跨族由prebuild_dangling_starts生成该族悬空，release挂prologue）。
    // 当年"(b)不归巢引发闪烁/归巢制为已验证绿色结构"的裁定作废：闪烁真因=悬空signal值序与
    // 提交序倒挂被Phase8前缀max去重放大（悬空槽案终局破案），空批/不归巢结构从头就是对的。
    // 收益：import省每帧一次QFO往返；双向悬空成为常态，由StepB的帧首acquire切批防御保序
    //（pass_execution_phase：含帧首跨族acquire的pass不得留在sortKey=0的prologue批内）

    // 刀2a：新模型帧末——import资源tracker终态回写registry（merge换代保未触键旧记录）
    write_back_sync_records(graph);

#if ENABLE_RDG_AUDIT
    // 新模型自检（刀2a）：同一队列时间线上signal按schedule_order严格递增——由B的分配公式
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

    // ===== 悬空起点（取证补遗：原dump只遍历pass节点，悬空release不可见——布局案2026-10-26）=====
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

        // 刀2a①：同步节点推导（wait聚合+悬空判定/落账+signal分配）——先于屏障两阶段：
        // 资源族判定用的是访问前owner_family/种子（屏障逻辑此时尚未把它改写为转移后族）
        result_.pass_sync_nodes[pass] = build_sync_node(pass, res_info, target_queue, executor);

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

        // 刀2a②：signal已定，按read/write推进tracker（与屏障生成数据解耦，顺序无耦合）
        advance_sync_trackers(res_info, target_queue,
            result_.pass_sync_nodes[pass].signal.second, executor);
    }
}

void BarrierGenerationPhase::process_access(RDGPassNodeRef pass, const ResourceAccessInfo& access, uint32_t target_queue, bool after_pass)
{
    if (access.resource == nullptr) return;

    const uint32_t target_family = queue_schedule_.get_queue_family_index(target_queue); 

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
        const RHIResourceState before = tracker.states[tracker.index(mip_base, array_base)];
        bool stateChange = false;
        bool uavToUav = false;
        for (uint32_t mip = mip_base; mip < mip_base + mip_count && !(stateChange || uavToUav); mip++)
        {
            for (uint32_t array = array_base; array < array_base + array_count; array++)
            {
                const RHIResourceState current = tracker.states[tracker.index(mip, array)];
                if (current != access.resource_state) { stateChange = true; break; }
                if (current == RESOURCE_STATE_UNORDERED_ACCESS && access.resource_state == RESOURCE_STATE_UNORDERED_ACCESS)// UAV→UAV 同状态也需要写后读可见性
                {
                    uavToUav = true; break;
                }
            }
        }

        const uint32_t source_queue = tracker.last_pass != nullptr
            ? queue_schedule_.get_pass_queue_index(tracker.last_pass) : target_queue;
        // 族归属：直接读所有权追踪（帧首=池族/imported族初值；帧内=最近转移后的现任族）。
        // 与旧"由last_pass队列推导"等价（见TextureStateTracker::owner_family注释）
        const uint32_t source_family = tracker.owner_family;
        const bool cross_queue = tracker.last_pass != nullptr && source_queue != target_queue;
        const bool cross_family = source_family != RHI_QUEUE_FAMILY_IGNORED && source_family != target_family;

        const bool is_subresource = !(mip_base == 0 && mip_count == tracker.mip_levels &&
                                      array_base == 0 && array_count == tracker.array_layers);
        const TextureSubresourceRange subresource = {
            .aspect = access.aspect,   // 声明的aspect（NONE=整资源→RHI按格式回退）；此前恒NONE，[sub]屏障全被放大成整图
            .baseMipLevel = mip_base,
            .levelCount = mip_count,
            .baseArrayLayer = array_base,
            .layerCount = array_count };

        // 帧首跨族（池族≠本队列族；归巢机制下池族恒为graphics族）：帧末归巢release已把所有权
        // 交还graphics，帧首首个使用者族G≠graphics时需要 acquire(graphics→G)——配对的
        // release(graphics→G)由Phase 8录进graphics流起点的prologue（受prologueDone信号量边保护）
        const bool frame_start_cross_family = tracker.last_pass == nullptr && cross_family;

        if ((cross_queue || frame_start_cross_family) && cross_family && texture->info.sharingMode != RESOURCE_SHARING_TYPE_CONCURRENT)
        {
            // 跨队列异族：EXCLUSIVE纹理须由生产者队列release + 消费者队列acquire完成所有权转移。
            //（CONCURRENT纹理在此短路：owner恒IGNORED下cross_family已恒假，此条件为防御性显式门——
            //  防未来owner泄漏路径对CONCURRENT图像发族对屏障，validation层会判非法）
            // ★转移在acquire侧（2026-10-26布局案定罪修复）：release=纯所有权转移(before,before)，
            // acquire携带真实布局转移(before→目标态)。原"release转移+acquire同态(A,A)"形态把布局
            // 转移落在生产者CB——VVL按提交序追踪布局、不认信号量执行序，跨队列批次交错时消费方
            // CB先于转移CB入队即报"current layout错"（多队列布局错误风暴的唯一根因，单队列消融
            // 0错误实证）。转移挪到acquire后：屏障与使用点同CB紧贴，对提交序完全免疫。
            // QFO配对约束acquire.old==release.new由构造保证（两侧同取before）
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
                release.after_state = before;    // 纯所有权转移，布局不动（转移在acquire侧）
                release.after_pass = true;      // release挂生产者pass之后（其output收敛屏障之后）
                release.is_subresource = is_subresource;
                release.subresource = subresource;

                std::vector<BarrierBatch>& producer_batches = result_.pass_sync_nodes[tracker.last_pass].batches;
                if (producer_batches.empty()) producer_batches.push_back({{}, EBarrierType::ResourceTransition});
                producer_batches.front().barriers.push_back(release);
                result_.total_barriers++;
            }

            RDGBarrier acquire{};
            acquire.resource = texture;
            acquire.type = EBarrierType::ResourceTransition;
            acquire.source_pass = tracker.last_pass;     // 帧首跨族时为nullptr（所有权来自owner_family初值）
            acquire.target_pass = pass;
            acquire.source_queue = source_queue;
            acquire.target_queue = target_queue;
            acquire.src_family = source_family;
            acquire.dst_family = target_family;
            acquire.is_subresource = is_subresource;
            acquire.subresource = subresource;
            acquire.after_pass = false;     // acquire挂消费者pass之前（即使output边访问——消费在本pass）

            acquire.before_state = before;                  // ==release.new（QFO配对约束）
            acquire.after_state = access.resource_state;    // 真实布局转移在消费队列完成

            std::vector<BarrierBatch>& consumer_batches = result_.pass_sync_nodes[pass].batches;
            if (consumer_batches.empty()) consumer_batches.push_back({{}, EBarrierType::ResourceTransition});
            consumer_batches.front().barriers.push_back(acquire);
            result_.total_barriers++;

            // 所有权随转移对更新为目标族（后续同族访问不再发转移——判定读owner_family而非
            // 由last_pass推导，见tracker注释）
            tracker.owner_family = target_family;
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

                auto& batches = result_.pass_sync_nodes[pass].batches;
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
        if (tracker.owner_family == RHI_QUEUE_FAMILY_IGNORED && texture->info.sharingMode != RESOURCE_SHARING_TYPE_CONCURRENT)
            tracker.owner_family = target_family;   // 初值IGNORED（未指定所有权，如新池条目）：
                                                    // 首触采纳本族为基线——与旧"由last_pass队列推导"
                                                    // 等价（旧推导在第二次访问即从触碰者队列恢复族属）。
                                                    // CONCURRENT纹理不采纳——owner须恒IGNORED（见帧首初始化注释）
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

            auto& batches = result_.pass_sync_nodes[pass].batches;
            if (batches.empty()) batches.push_back({{}, EBarrierType::ResourceTransition});
            batches.front().barriers.push_back(barrier);
            result_.total_barriers++;
        }

        tracker.state = access.resource_state;
        tracker.last_pass = pass;
    }
}

// —— 时间线双signal模型（刀2a）/////////////////////////////////////////////////////////////////////////

// 族→队列反查（族:队列=1:1前提下的代表队列——悬空起点的挂靠队列）
static uint32_t family_queue_index(const QueueSchedule& schedule, uint32_t family)
{
    for (const QueueInfo& qi : schedule.get_schedule_result().all_queues)
        if (qi.family_index == family)
            return qi.index;
    return 0;   // 启用族必有队列，不可达
}

void BarrierGenerationPhase::prebuild_dangling_starts(PerFrameCommonResourceRef executor, uint32_t graphics_family)
{
    // 沿全局调度序线性扫（与走查同过滤：isCulled），逐纹理定格首触access——悬空的存在性/成员/
    // signal值/waits/帧首release/+1落账全部在此定格，走查首触特判整删。buffer无悬空不扫。
    // 判定（与旧首触特判同源三分支）：种子族（import+registry命中=记录owner族 / import未命中
    // =graphics / 池化=池条目initFamily）≠ 首触者族 && EXCLUSIVE → 该族聚合虚拟节点。
    // CONCURRENT显式排除：设计上owner恒IGNORED免QFO，防registry记录的sync_queue带回族域
    const auto& topo_order = queue_schedule_.get_schedule_result().schedule_order;
    std::unordered_set<RDGTextureNodeRef> firstSeen;

    for (RDGPassNodeRef pass : topo_order)
    {
        if (!pass || pass->isCulled) continue;
        const PassResourceInfo* res_info = pass_info_analysis_.get_resource_info(pass);
        if (!res_info) continue;

        const uint32_t target_queue = queue_schedule_.get_pass_queue_index(pass);
        const uint32_t target_family = queue_schedule_.get_queue_family_index(target_queue);

        for (const auto& access : res_info->resource_accesses)
        {
            if (access.resource == nullptr) continue;
            if (access.resource->NodeType() != RDG_RESOURCE_NODE_TYPE_TEXTURE) continue;
            auto texture = static_cast<RDGTextureNodeRef>(access.resource);
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
            else if (texture->IsImported())            // import未命中（上上帧起未触）：静止graphics
            {
                seed_family = graphics_family;
                owner_queue = 0;
            }
            else                                       // 池化：initFamily初值（CONCURRENT=IGNORED不悬空）
            {
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
            // 并入waits：资源seed记录逐线取max(same,cross)——release须排在上帧对该资源的
            // 一切使用之后（QFO转移点的保守等待）
            for (const auto& [timeline, pair] : tracker.sync_signals)
            {
                const uint64_t value = std::max(pair.first, pair.second);
                uint64_t* slot = nullptr;
                for (auto& [tl, v] : dangling.waits)
                    if (tl == timeline) { slot = &v; break; }
                if (slot != nullptr) *slot = std::max(*slot, value);
                else dangling.waits.emplace_back(timeline, value);
            }
            // +1落账（point 10）：patch资源所在队列时间线的异族值——异族首触者经fold_record
            // 取到它；同族后续访问取同族值不等它（read语义）
            tracker.sync_signals[dangling.signal.first].second = dangling.signal.second;
            tracker.sync_queue = owner_queue;      // 悬空触碰：挂靠队列即资源所在队列

            // 帧首release屏障（QFO转移，无布局变化）：挂悬空节点batches，Phase 8录进该族队列
            // 流首prologue批发射。范围=首触access（与process_access帧首acquire按同一条access
            // 解析——QFO配对要求范围一致）。纯所有权(F,F)：F=首触前tracker态（import=registry
            // 记录/池化=池记终态），布局转移在配对acquire侧完成
            const uint32_t mip_base = access.mip_base;
            const uint32_t mip_count = access.mip_count == 0 ? tracker.mip_levels : access.mip_count;
            const uint32_t array_base = access.array_base;
            const uint32_t array_count = access.array_count == 0 ? tracker.array_layers : access.array_count;

            RDGBarrier release{};
            release.resource = texture;
            release.type = EBarrierType::ResourceTransition;
            release.source_pass = nullptr;         // 悬空起点=帧首虚拟节点，无本帧生产者
            release.target_pass = pass;             // 首触消费者（日志锚定）
            release.source_queue = owner_queue;
            release.target_queue = target_queue;
            release.src_family = seed_family;
            release.dst_family = target_family;
            release.before_state = tracker.states[tracker.index(mip_base, array_base)];
            release.after_state = tracker.states[tracker.index(mip_base, array_base)];
            release.is_subresource = !(mip_base == 0 && mip_count == tracker.mip_levels &&
                                       array_base == 0 && array_count == tracker.array_layers);
            release.subresource = { access.aspect, mip_base, mip_count, array_base, array_count };
            if (dangling.batches.empty())
                dangling.batches.push_back({{}, EBarrierType::ResourceTransition});
            dangling.batches.front().barriers.push_back(release);
            result_.total_barriers++;
        }
    }
}

RDGSyncNode BarrierGenerationPhase::build_sync_node(
    RDGPassNodeRef pass, const PassResourceInfo* res_info,
    uint32_t target_queue, PerFrameCommonResourceRef executor)
{
    const uint32_t target_family = queue_schedule_.get_queue_family_index(target_queue);
    const RHISemaphoreRef& target_timeline = executor->queueSlots[target_queue].timeline;

    // wait聚合表：时间线→包含集（逐线max）。键持ref顺带跨帧保活旧时间线对象
    std::unordered_map<RHISemaphoreRef, uint64_t> waits;

    auto fold_record = [&](const std::unordered_map<RHISemaphoreRef, std::pair<uint64_t, uint64_t>>& signals,
                           uint32_t resource_family)
    {
        // CONCURRENT（族IGNORED）无所有权概念：按"异族语义"取cross——read也推进该值，跨队列
        // WAR（读→写）由此覆盖；write推双值天然覆盖RAW/WAW。read-read跨队列会多等一次
        // （过同步，无害）。EXCLUSIVE正常走同族/异族选值（同族值=最后write，同队列冲突由
        // 状态屏障覆盖；异族值=最后任意访问，跨队列全冲突类型覆盖）
        const bool same_family = resource_family != RHI_QUEUE_FAMILY_IGNORED && resource_family == target_family;
        for (const auto& [timeline, pair] : signals)
        {
            uint64_t& slot = waits[timeline];
            slot = std::max(slot, same_family ? pair.first : pair.second);
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

            // 悬空起点与+1落账已由prebuild_dangling_starts在走查前定格（刀A）——异族首触者的
            // 悬空等待已在种子sync_signals里，fold直接取到

            // 资源族=访问前现任族（owner_family由屏障逻辑维护：跨族acquire后才改写为目标族——
            // 本pass等的是转移前所有者留下的值）
            fold_record(tracker.sync_signals, tracker.owner_family);
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
                : queue_schedule_.get_queue_family_index(tracker.sync_queue));
        }
    }

    RDGSyncNode node;
    // signal = wait+1（实现=max(wait[本线], B[本线])+1并回写B——B管跨帧值序与空wait兜底，规则①）
    node.signal.first = target_timeline;
    node.signal.second = cross_frame_registry_.alloc_timeline_signal(target_timeline.get(), waits[target_timeline]);
    node.waits.reserve(waits.size());
    for (const auto& [timeline, value] : waits)
        node.waits.emplace_back(timeline, value);
    return node;
}

void BarrierGenerationPhase::advance_sync_trackers(const PassResourceInfo* res_info, uint32_t target_queue,
    uint64_t signal, PerFrameCommonResourceRef executor)
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
            // 落在pass所在队列时间线上：read只推异族（read-read跨队列也要经release/acquire链）；
            // write推双值（写后读的同族等待链）。同pass多access逐条覆盖，终值=最后一条的访问类型
            // ——write在前read在后不会回退same值（read不写same）
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
    // registry键与import身份在用点现取（刀A：sync_key/sync_touched已删）
    // texture与buffer分写：texture记录额外带逐子资源终态（布局动态记载），buffer暂无布局维度
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
