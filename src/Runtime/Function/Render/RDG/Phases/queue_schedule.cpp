#include "queue_schedule.h"
#include "pass_dependency_analysis.h"

#include "Function/Global/EngineContext.h"
#include "lemon/list_graph.h"

#include <cfloat>
#include <string>
#include <unordered_map>

QueueSchedule::QueueSchedule(const PassDependencyAnalysis& dependency_analysis,
    const PassInfoAnalysis& pass_info_analysis,
    const QueueScheduleConfig& cfg,
    RHIRenderQueryRef timingQuery)
    : config(cfg)
    , dependency_analysis(dependency_analysis)
    , pass_info_analysis(pass_info_analysis)
    , timingQuery(std::move(timingQuery))
{}

QueueSchedule::~QueueSchedule() = default;

void QueueSchedule::reset_for_frame()
{
    all_queues.clear();
    schedule_result.queue_schedules.clear();
    schedule_result.pass_queue_assignments.clear();
    schedule_result.schedule_order.clear();
    ownership_edges_.clear();
    // 注：schedule_result.all_queues 是指向成员 all_queues 的 span，本帧由
    // assign_passes_to_queues 重新赋值，reset 后悬空属预期（下游都在 Phase 3 完成后读取）
}

void QueueSchedule::on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor) 
{
    ENGINE_TIME_SCOPE(QueueSchedule::on_execute);
    // 查询队列
    query_queue_capabilities(graph);

    // 分配 Pass
    assign_passes_to_queues(graph);

    // 可选：输出调试信息
    if (config.enable_debug_output) {
        dump_timeline_result("Timeline Schedule", schedule_result);
    }
}

std::vector<RHIQueueRef> QueueSchedule::QueryConfiguredQueues(const QueueScheduleConfig& config)
{
    auto& Backend = EngineContext::RHI();
    std::vector<RHIQueueRef> queues;

    if (config.enable_graphic_queues)
        for (uint32_t i = 0; i < config.enable_graphic_queues; ++i)
            if (auto gfx_queue = Backend->GetQueue({ QUEUE_TYPE_GRAPHICS, i }))
                queues.push_back(gfx_queue);

    if (config.enable_async_compute_queues)
        for (uint32_t i = 0; i < config.enable_async_compute_queues; ++i)
            if (auto cmpt_queue = Backend->GetQueue({ QUEUE_TYPE_COMPUTE, i }))
                queues.push_back(cmpt_queue);

    if (config.enable_copy_queues)
        for (uint32_t i = 0; i < config.enable_copy_queues; ++i)
            if (auto cpy_queue = Backend->GetQueue({ QUEUE_TYPE_TRANSFER, i }))
                queues.push_back(cpy_queue);

    return queues;
}

void QueueSchedule::query_queue_capabilities(RDGDependencyGraphRef graph)
{
    // 清空队列信息（简化为单一数组），队列集合由 QueryConfiguredQueues 统一决定
    all_queues.clear();
    const std::vector<RHIQueueRef> configured = QueryConfiguredQueues(config);

    for (uint32_t queue_index = 0; queue_index < configured.size(); queue_index++)
    {
        const RHIQueueRef& handle = configured[queue_index];

        QueueInfo info{};
        info.index = queue_index;
        info.handle = handle;
        info.family_index = handle->GetFamilyIndex();

        // 能力位按RHI队列类型归位（graphics族全能；compute族无present；transfer族仅copy）
        switch (handle->GetQueueType())
        {
        case QUEUE_TYPE_GRAPHICS:
            info.type = ERenderGraphQueueType::Graphics;
            info.supports_graphics = true;
            info.supports_compute = true;
            info.supports_copy = true;
            info.supports_present = true;
            break;
        case QUEUE_TYPE_COMPUTE:
            info.type = ERenderGraphQueueType::AsyncCompute;
            info.supports_compute = true;
            info.supports_copy = true;
            break;
        case QUEUE_TYPE_TRANSFER:
            info.type = ERenderGraphQueueType::Copy;
            info.supports_copy = true;
            break;
        default:
            break;
        }

        all_queues.push_back(info);
    }
}

// 给定一个 Pass，决定它希望在哪种类型的队列上执行。
ERenderGraphQueueType QueueSchedule::classify_pass(RDGPassNodeRef pass)
{
    // 1. Present Pass必须在Graphics队列
    if (pass->NodeType() == RDGPassNodeType::RDG_PASS_NODE_TYPE_PRESENT) {
        return ERenderGraphQueueType::Graphics;
    }

    // 2. Render Pass必须在Graphics队列
    if (pass->NodeType() == RDGPassNodeType::RDG_PASS_NODE_TYPE_RENDER) {
        return ERenderGraphQueueType::Graphics;
    }

    // 3. Copy Pass优先Copy队列（如果标记为可独立执行）
    if (pass->NodeType() == RDGPassNodeType::RDG_PASS_NODE_TYPE_COPY && config.enable_copy_queues) {
        auto* copy_pass = static_cast<RDGCopyPassNodeRef>(pass);
        if (!copy_pass->IsGenerateMip()) {        // 非 mipmap 拷贝才能去 Copy 队列
            return ERenderGraphQueueType::Copy;
        }
    }

    // 4. Compute Pass仅基于手动标记
    if (pass->NodeType() == RDGPassNodeType::RDG_PASS_NODE_TYPE_COMPUTE && config.enable_async_compute_queues)
    {
        if (!pass->has_flags(RDGPassFlags::ForceGraphicsQueue)) {
            return ERenderGraphQueueType::AsyncCompute;   // 现行为：全部compute pass上async队列
        }
    }

    // 默认分配到Graphics队列
    return ERenderGraphQueueType::Graphics;
}

void QueueSchedule::assign_passes_to_queues(RDGDependencyGraphRef graph)
{
    // 拷贝队列信息到结果
    schedule_result.all_queues = all_queues;
    // 每个队列一个 pass 列表
    schedule_result.queue_schedules.resize(all_queues.size());

    if (config.use_heft)
        schedule_with_heft(graph);
    else
        assign_passes_using_topology();
}

// HEFT调度 /////////////////////////////////////////////////////////////////////////
//
// 取代classify（NodeType+白名单）+同层注册序的三重偶然分派（2026-09-11）：
//   ranku（关键链优先）+ EFT（各合格队列最早完成时间）+ 资源所有权表（EXCLUSIVE纹理跨族
//   访问的互斥排队——异族访问须等当前持有者完成+同步开销）。
// 调度完成后所有权链的相邻异族访问者对回写为SchedulingOrderEdge（RDGCompiler协调进Phase2
// 并重算拓扑）——它们是QFO所有权链的执行序载体与SSIS同步点来源（读者串行化由"构造序"改为
// "权衡调度后填写"的落点）。
//
// 注：cull的pass也在调度集内（isCulled对QueueSchedule不可见）——fallback权重伪占位，
// Step A的流构建照旧剔除；权重EMA为纯exec对（不含等待——等待由本调度器自己建模）
void QueueSchedule::schedule_with_heft(RDGDependencyGraphRef graph)
{
    std::vector<RDGPassNodeRef>& passes = get_passes(graph);
    const uint32_t queueCount = static_cast<uint32_t>(all_queues.size());
    if (queueCount == 0 || passes.empty()) return;

    // ---- 调度图（lemon）：节点=pass，边=pass级依赖（Phase2静态边：RAW/WAW/WAR）----
    lemon::ListDigraph dag;
    lemon::ListDigraph::NodeMap<RDGPassNodeRef> passOf(dag, nullptr);
    std::unordered_map<RDGPassNodeRef, lemon::ListDigraph::Node> nodeOf;
    nodeOf.reserve(passes.size() * 2);
    for (RDGPassNodeRef pass : passes)
    {
        const lemon::ListDigraph::Node n = dag.addNode();
        passOf[n] = pass;
        nodeOf.emplace(pass, n);
    }
    std::unordered_map<RDGPassNodeRef, std::vector<RDGPassNodeRef>> successors;
    for (RDGPassNodeRef pass : passes)
    {
        const PassDependencies* deps = dependency_analysis.get_pass_dependencies(pass);
        if (!deps) continue;
        for (RDGPassNodeRef pred : deps->dependent_passes)
        {
            auto found = nodeOf.find(pred);
            if (found == nodeOf.end()) continue;      // 前驱不在本帧pass集（防御）
            dag.addArc(found->second, nodeOf[pass]);
            successors[pred].push_back(pass);
        }
    }

    // ---- 权重（快照化的历史耗时）----
    // 快照法保证帧间决策稳定：EMA逐帧波动→EFT决策帧间翻转→池化(created)纹理跨帧复用同
    // 物理内存且细链只回看一帧，跨帧跨队列WAR靠"分配帧间稳定"隐式兜底，翻转即触发竞态
    // （画面闪烁，帧内视角裁判均不可见）。同图+同权重⇒ranku序/EFT/tie-break全确定性⇒同调度。
    // 快照每300帧刷新一次（刷新帧存在一次性迁移窗口，1/300概率vs每帧闪）
    static std::unordered_map<std::string, double> s_weightSnapshot;
    static uint32_t s_snapshotAge = 0;
    if (s_snapshotAge == 0)
    {
        for (RDGPassNodeRef pass : passes)
        {
            const std::string name(pass->Name());
            const double live = timingQuery
                ? timingQuery->GetPassDurationMs(name, config.heft_fallback_duration_ms)
                : config.heft_fallback_duration_ms;
            s_weightSnapshot[name] = live;
        }
    }
    if (++s_snapshotAge >= 300) s_snapshotAge = 0;

    std::unordered_map<RDGPassNodeRef, double> weight;
    for (RDGPassNodeRef pass : passes)
    {
        const auto found = s_weightSnapshot.find(std::string(pass->Name()));
        weight[pass] = found != s_weightSnapshot.end() ? found->second : config.heft_fallback_duration_ms;
    }

    // ---- ranku（向上权重，逆拓扑DP）：关键链前驱优先调度 ----
    const auto& topoOrder = dependency_analysis.get_logical_topology_result().logical_topological_order;
    std::unordered_map<RDGPassNodeRef, double> ranku;
    for (size_t i = topoOrder.size(); i-- > 0;)
    {
        RDGPassNodeRef v = topoOrder[i];
        double best = 0.0;
        for (RDGPassNodeRef s : successors[v])
        {
            const auto rs = ranku.find(s);
            best = std::max(best, config.heft_comm_cost_ms + (rs != ranku.end() ? rs->second : 0.0));
        }
        ranku[v] = weight[v] + best;
    }


    // ---- 调度态 ----
    std::vector<double> queueReady(queueCount, 0.0);                  // 各队列就绪时间（append式，第一版不回填空隙）
    std::unordered_map<RDGPassNodeRef, double> aft;                   // 各pass完成时间
    std::unordered_map<RDGPassNodeRef, uint32_t> assigned;
    std::unordered_map<RDGPassNodeRef, uint32_t> remainingPreds;
    std::vector<RDGPassNodeRef> ready;
    schedule_result.schedule_order.clear();
    schedule_result.schedule_order.reserve(passes.size());

    // 所有权表：资源 → 当前持有（族/完成时间）+ 访问链（回写边提取用）
    struct ChainEntry { RDGPassNodeRef pass; EResourceAccessType access; };
    struct OwnershipState
    {
        uint32_t family = RHI_QUEUE_FAMILY_IGNORED;
        double availableTime = 0.0;
        std::vector<ChainEntry> chain;
    };
    std::unordered_map<RDGResourceNodeRef, OwnershipState> ownership;

    for (RDGPassNodeRef pass : passes)
    {
        uint32_t cnt = 0;
        if (const PassDependencies* deps = dependency_analysis.get_pass_dependencies(pass))
            for (RDGPassNodeRef pred : deps->dependent_passes)
                if (nodeOf.count(pred) > 0) cnt++;
        remainingPreds[pass] = cnt;
        if (cnt == 0) ready.push_back(pass);
    }

    // ---- 主循环：ranku降序取就绪pass，EFT最小队列（平局小下标=graphics先，确定性tie-break）----
    uint32_t scheduled = 0;
    while (scheduled < passes.size())
    {
        if (ready.empty())
        {
            ENGINE_LOG_WARN("QueueSchedule::schedule_with_heft: dependency cycle detected ({} scheduled / {})", scheduled, passes.size());
            break;
        }

        auto bestIt = ready.begin();
        for (auto it = ready.begin(); it != ready.end(); ++it)
        {
            const double rNew = ranku[*it], rBest = ranku[*bestIt];
            if (rNew > rBest + 1e-9 ||
                (rNew > rBest - 1e-9 && std::string_view((*it)->Name()) < std::string_view((*bestIt)->Name())))
                bestIt = it;
        }
        RDGPassNodeRef v = *bestIt;
        ready.erase(bestIt);

        // 资源访问全集（同资源多条访问合并为单条目，含写即ReadWrite）
        std::vector<std::pair<RDGResourceNodeRef, EResourceAccessType>> accesses;
        if (const PassResourceInfo* info = pass_info_analysis.get_resource_info(v))
        {
            std::unordered_map<RDGResourceNodeRef, EResourceAccessType> merged;
            for (const auto& acc : info->resource_accesses)
            {
                if (!acc.resource) continue;
                EResourceAccessType& slot = merged[acc.resource];
                const bool anyWrite = slot == EResourceAccessType::ReadWrite ||
                                      acc.access_type != EResourceAccessType::Read;
                slot = anyWrite ? EResourceAccessType::ReadWrite : EResourceAccessType::Read;
            }
            accesses.reserve(merged.size());
            for (const auto& [res, acc] : merged)
                accesses.emplace_back(res, acc);
        }

        const PassDependencies* vDeps = dependency_analysis.get_pass_dependencies(v);
        double bestEft = DBL_MAX;
        uint32_t bestQ = 0;
        std::string eftTrace;   // debug决策日志
        std::vector<double> eftOf(queueCount, DBL_MAX);   // 各合格队列候选（滞后判断用）

        for (uint32_t q = 0; q < queueCount; q++)
        {
            if (!is_queue_eligible(v, q)) continue;

            double est = queueReady[q];
            if (vDeps)
            {
                for (RDGPassNodeRef pred : vDeps->dependent_passes)
                {
                    const auto fa = assigned.find(pred);
                    if (fa == assigned.end()) continue;          // 未调度前驱（防御——就绪集保证不存在）
                    if (nodeOf.count(pred) == 0) continue;
                    const double c = (fa->second != q) ? config.heft_comm_cost_ms : 0.0;
                    est = std::max(est, aft[pred] + c);
                }
            }
            // 所有权约束：访问的资源被异族持有 → 等持有者完成（EXCLUSIVE跨族互斥的排队）
            const uint32_t vFamily = all_queues[q].family_index;
            double ownershipWait = 0.0;
            for (const auto& [res, acc] : accesses)
            {
                (void)acc;
                const auto fo = ownership.find(res);
                if (fo == ownership.end()) continue;
                if (fo->second.family == RHI_QUEUE_FAMILY_IGNORED || fo->second.family == vFamily) continue;
                ownershipWait = std::max(ownershipWait, fo->second.availableTime + config.heft_comm_cost_ms);
            }
            est = std::max(est, ownershipWait);

            const double eft = est + weight[v];
            eftOf[q] = eft;
            if (config.enable_debug_output)
                eftTrace += std::format(" q{}:est={:.2f}/own={:.2f}/eft={:.2f}", q, est, ownershipWait, eft);

            if (eft < bestEft - 1e-9 || (eft < bestEft + 1e-9 && q < bestQ))
            {
                bestEft = eft;
                bestQ = q;
            }
        }

        // 分配滞后（跨帧槽共享的上帧分配表，主线程串行访问）：EFT差在阈值内保持上帧队列——
        // 池化(created)纹理跨帧复用同物理内存且细链只回看一帧，跨帧跨队列WAR靠"分配帧间
        // 稳定"隐式兜底（旧classify天然满足）；EMA波动使EFT翻转即触发该竞态（画面闪烁，
        // 帧内视角的裁判均不可见——2026-09-12定案）
        static std::unordered_map<std::string, uint32_t> s_lastAssignment;
        const std::string passName(v->Name());
        const auto lastIt = s_lastAssignment.find(passName);
        if (lastIt != s_lastAssignment.end() && lastIt->second != bestQ &&
            lastIt->second < queueCount && eftOf[lastIt->second] < bestEft + config.heft_hysteresis_ms)
        {
            bestEft = eftOf[lastIt->second];
            bestQ = lastIt->second;
            if (config.enable_debug_output) eftTrace += std::format(" [hyst->q{}]", bestQ);
        }
        s_lastAssignment[passName] = bestQ;

        assigned[v] = bestQ;
        aft[v] = bestEft;
        queueReady[bestQ] = bestEft;
        schedule_result.queue_schedules[bestQ].push_back(v);
        schedule_result.pass_queue_assignments[v] = bestQ;
        schedule_result.schedule_order.push_back(v);   // 全局调度序（Step A的canonical序）
        scheduled++;

        if (config.enable_debug_output)
            ENGINE_LOG_INFO("[HEFT] '{}' -> q{} eft={:.2f}{}", v->Name(), bestQ, bestEft, eftTrace);

        // 所有权随访问迁移（链记录访问序）
        for (const auto& [res, acc] : accesses)
        {
            OwnershipState& st = ownership[res];
            st.family = all_queues[bestQ].family_index;
            st.availableTime = bestEft;
            st.chain.push_back({ v, acc });
        }

        // 解锁后继
        for (RDGPassNodeRef s : successors[v])
            if (--remainingPreds[s] == 0) ready.push_back(s);
    }

    // ---- 所有权链相邻异族对 → 回写边（同族对靠拓扑传递性，不建边）----
    ownership_edges_.clear();
    for (const auto& [res, st] : ownership)
    {
        for (size_t i = 1; i < st.chain.size(); i++)
        {
            const auto& prev = st.chain[i - 1];
            const auto& next = st.chain[i];
            const uint32_t qPrev = assigned[prev.pass];
            const uint32_t qNext = assigned[next.pass];
            if (all_queues[qPrev].family_index == all_queues[qNext].family_index) continue;
            ownership_edges_.push_back({ prev.pass, next.pass, res, prev.access, next.access });
        }
    }

    if (config.enable_debug_output)
        ENGINE_LOG_INFO("[HEFT] ownership order edges: {} (cross-family adjacent pairs)",
            ownership_edges_.size());
}

// pass在指定队列上是否合格（能力约束——classify规则的集合化：HEFT在各合格队列上算EFT）
bool QueueSchedule::is_queue_eligible(RDGPassNodeRef pass, uint32_t queueIndex)
{
    if (queueIndex >= all_queues.size()) return false;
    const QueueInfo& q = all_queues[queueIndex];

    switch (pass->NodeType())
    {
    case RDGPassNodeType::RDG_PASS_NODE_TYPE_PRESENT:
        return q.supports_present;
    case RDGPassNodeType::RDG_PASS_NODE_TYPE_RENDER:
        return q.supports_graphics;
    case RDGPassNodeType::RDG_PASS_NODE_TYPE_COPY:
    {
        // mipmap拷贝须graphics（generateMip的mip链屏障），其余可copy队列
        auto* copy_pass = static_cast<RDGCopyPassNodeRef>(pass);
        // [二分]锁定classify分配时copy也钉graphics——classify在copy队列关闭时非mip copy全落
        // graphics（classify_pass默认分支），而supports_copy在graphics/compute队列上都为true，
        // 不钉则Bloom Copy等pass的EFT自由度污染"等分配"前提
        if (config.heft_lock_classify_assignment)
            return q.supports_graphics;
        return copy_pass->IsGenerateMip() ? q.supports_graphics : q.supports_copy;
    }
    case RDGPassNodeType::RDG_PASS_NODE_TYPE_COMPUTE:
        // ForceGraphicsQueue钉graphics（与classify白名单语义一致——Present/UI/手工钉位的既有知识）；
        // 无flag的compute pass在graphics与async compute上都合格（EFT权衡取代"全去async"）。
        // [二分诊断]锁定classify分配时仅AsyncCompute合格（隔离分配自由度面）
        if (pass->has_flags(RDGPassFlags::ForceGraphicsQueue))
            return q.supports_graphics;
        if (config.heft_lock_classify_assignment)
            return q.type == ERenderGraphQueueType::AsyncCompute;
        return q.supports_compute;
    default:
        return q.supports_graphics;
    }
}

// 利用 PassDependencyAnalysis 的依赖级别信息，按级别顺序遍历所有 Pass，将每个 Pass 分配到合适的队列
void QueueSchedule::assign_passes_using_topology()
{
    // 获取逻辑拓扑排序结果
    const auto& topology_result = dependency_analysis.get_logical_topology_result();

    //ENGINE_LOG_INFO("QueueSchedule: Using topology-based scheduling with {} dependency levels",
    //    topology_result.max_logical_dependency_depth + 1);

    // 按依赖级别顺序调度Pass，确保依赖正确性
    for (const auto& level : topology_result.logical_levels)
    {
        //ENGINE_LOG_INFO("  Processing dependency level {} with {} passes",
        //    level.level, static_cast<uint32_t>(level.passes.size()));

        // 在同一依赖级别内，按照拓扑顺序分配Pass到队列
        for (auto* pass : level.passes)
        {
            // 分类Pass并找到合适的队列
            ERenderGraphQueueType preferred_queue_type = classify_pass(pass);
            uint32_t target_queue_index;

            // 根据队列类型找到实际的队列索引
            switch (preferred_queue_type)
            {
            case ERenderGraphQueueType::Graphics:
                target_queue_index = find_graphics_queue();
                break;
            case ERenderGraphQueueType::AsyncCompute:
                target_queue_index = find_least_loaded_compute_queue();
                break;
            case ERenderGraphQueueType::Copy:
                target_queue_index = find_copy_queue();
                break;
            default:
                target_queue_index = find_graphics_queue();
                break;
            }

            // 将Pass添加到选定的队列
            if (target_queue_index < schedule_result.queue_schedules.size())
            {
                schedule_result.queue_schedules[target_queue_index].push_back(pass);
                schedule_result.pass_queue_assignments[pass] = target_queue_index;

                //ENGINE_LOG_INFO("    Assigned pass '{}' to {} queue (index {})",
                //    pass->Name(),
                //    get_queue_type_name(all_queues[target_queue_index].type),
                //    target_queue_index);
            }
            else
            {
                //SPDLOG_ERROR("QueueSchedule: Invalid queue index {} for pass '{}'",
                //    target_queue_index, pass->Name());
            }
        }
    }

    //ENGINE_LOG_INFO("QueueSchedule: Topology-based scheduling completed");
}

uint32_t QueueSchedule::find_graphics_queue() const
{
    for (uint32_t i = 0; i < all_queues.size(); ++i) {
        if (all_queues[i].type == ERenderGraphQueueType::Graphics) {
            return i;
        }
    }
    return 0; // 应该总是有Graphics队列
}

uint32_t QueueSchedule::find_least_loaded_compute_queue() const
{
    // 简化轮询：找到第一个计算队列即可，避免复杂的负载计算
    static uint32_t next_compute_index = 0;

    std::vector<uint32_t> compute_queues;
    for (uint32_t i = 0; i < all_queues.size(); ++i) {
        if (all_queues[i].type == ERenderGraphQueueType::AsyncCompute) {
            compute_queues.push_back(i);
        }
    }

    if (compute_queues.empty()) {
        return find_graphics_queue(); // 回退
    }

    uint32_t queue_idx = compute_queues[next_compute_index % compute_queues.size()];
    next_compute_index++;
    return queue_idx;
}

uint32_t QueueSchedule::find_copy_queue() const
{
    for (uint32_t i = 0; i < all_queues.size(); ++i) {
        if (all_queues[i].type == ERenderGraphQueueType::Copy) {
            return i;
        }
    }
    return find_graphics_queue(); // 回退
}

void QueueSchedule::dump_timeline_result(const char* title, const TimelineScheduleResult& R) const
{
    ENGINE_LOG_INFO("═══════════════════════════════════════");
    ENGINE_LOG_INFO("{}", title);
    ENGINE_LOG_INFO("═══════════════════════════════════════");

    // 打印队列调度信息
    ENGINE_LOG_INFO(" Queue Schedules ({} queues):", (int)R.queue_schedules.size());
    for (size_t i = 0; i < R.queue_schedules.size(); ++i) {
        const auto& queue_schedule = R.queue_schedules[i];
        const auto& queue_info = R.all_queues[i];
        const char* queue_name = get_queue_type_name(queue_info.type);

        // 为多队列类型添加索引标识
        std::string queue_display_name;
        if (queue_info.type == ERenderGraphQueueType::AsyncCompute && all_queues.size() > 1) {
            uint32_t async_idx = 0;
            for (size_t k = 0; k < i; ++k) {
                if (all_queues[k].type == ERenderGraphQueueType::AsyncCompute)
                    async_idx++;
            }
            queue_display_name = std::format("{}#{}", queue_name, async_idx);
        }
        else {
            queue_display_name = std::string(queue_name);
        }

        ENGINE_LOG_INFO("  [{}] {} Queue (index={}, {} passes):",
            i, queue_display_name.c_str(), queue_info.index, (int)queue_schedule.size());

        for (size_t j = 0; j < queue_schedule.size(); ++j) {
            auto* pass = queue_schedule[j];
            const char* pass_type_name = "Unknown";

            switch (pass->NodeType()) {
            case RDGPassNodeType::RDG_PASS_NODE_TYPE_RENDER: pass_type_name = "Render"; break;
            case RDGPassNodeType::RDG_PASS_NODE_TYPE_COMPUTE: pass_type_name = "Compute"; break;
            case RDGPassNodeType::RDG_PASS_NODE_TYPE_COPY: pass_type_name = "Copy"; break;
            case RDGPassNodeType::RDG_PASS_NODE_TYPE_PRESENT: pass_type_name = "Present"; break;
            default: break;
            }

            ENGINE_LOG_INFO("    [{}] {} Pass (name={})", j, pass_type_name, pass->Name());
        }
    }

    // 打印Pass映射统计
    ENGINE_LOG_INFO("");
    ENGINE_LOG_INFO("Pass Assignment Summary:");

    uint32_t graphics_count = 0, compute_count = 0, copy_count = 0;
    for (const auto& [pass, queue_idx] : R.pass_queue_assignments)
    {
        if (queue_idx < R.queue_schedules.size()) {
            auto queue_type = R.all_queues[queue_idx].type;
            switch (queue_type) {
            case ERenderGraphQueueType::Graphics: graphics_count++; break;
            case ERenderGraphQueueType::AsyncCompute: compute_count++; break;
            case ERenderGraphQueueType::Copy: copy_count++; break;
            default: break;
            }
        }
    }

    ENGINE_LOG_INFO(" Graphics Queue: {} passes", graphics_count);
    ENGINE_LOG_INFO(" AsyncCompute Queue: {} passes", compute_count);
    ENGINE_LOG_INFO(" Copy Queue: {} passes", copy_count);
    ENGINE_LOG_INFO(" Total Passes: {}", (int)R.pass_queue_assignments.size());
}