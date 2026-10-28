#include "queue_schedule.h"
#include "pass_dependency_analysis.h"

#include "Function/Global/EngineContext.h"

#include <cassert>
#include <cfloat>
#include <string>
#include <unordered_map>

QueueSchedule::QueueSchedule(PassDependencyAnalysis& dependency_analysis,
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
}

void QueueSchedule::on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor) 
{
    ENGINE_TIME_SCOPE(QueueSchedule::on_execute);
    // 查询队列能力
    query_queue_capabilities(graph);

    // 分配 Pass
    assign_passes_to_queues(graph);

    // 原Phase 3.5（HEFT协调步，自RDGCompiler并入）：所有权链序边回写Phase2 + 按调度序
    // 重建拓扑——回写边是QFO所有权链的执行序载体与SSIS同步点来源；拓扑序以调度序为唯一
    // 权威（下游生命期/tracker假设"拓扑序=执行序"）。判空仅防御空图/零队列早退（常态非空）
    if (!schedule_result.schedule_order.empty())
    {
        dependency_analysis.add_scheduling_order_edges(ownership_edges_);
        dependency_analysis.apply_schedule_order(schedule_result.schedule_order);
    }

    // 可选：输出调试信息
    if (config.enable_debug_output) {
        dump_timeline_result("Timeline Schedule", schedule_result);
    }
}

uint32_t QueueSchedule::get_pass_queue_index(RDGPassNodeRef pass) const
{
    // 调度宇宙含culled伪占位——全量pass必有分配，未命中=真bug。
    // assert+0兜底（原转发实现是end迭代器解引用UB，收编时一并修正）
    const auto found = schedule_result.pass_queue_assignments.find(pass);
    assert(found != schedule_result.pass_queue_assignments.end());
    return found != schedule_result.pass_queue_assignments.end() ? found->second : 0;
}

uint32_t QueueSchedule::get_queue_family_index(uint32_t queue_index) const
{
    if (queue_index >= all_queues.size()) return RHI_QUEUE_FAMILY_IGNORED;
    return all_queues[queue_index].family_index;
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
    // 清空队列信息，队列集合由 QueryConfiguredQueues 统一决定
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
void QueueSchedule::assign_passes_to_queues(RDGDependencyGraphRef graph)
{
    // 拷贝队列信息到结果
    schedule_result.all_queues = all_queues;
    // 每个队列一个 pass 列表
    schedule_result.queue_schedules.resize(all_queues.size());
 
    schedule_with_heft(graph);

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

    // ---- 调度数据：后继表（原lemon图遗迹已删——dag/passOf建成后零消费，算法全程手搓）----
    // successors=Phase2反向依赖表（dependent_by_passes）逐pass拷贝：ranku DP与就绪集解锁用。
    // 调度宇宙=本帧全量pass（含culled伪占位——isCulled对QueueSchedule不可见，Step A过滤；
    // Phase2依赖边只引用本帧pass，入度直接取dependent_passes.size()无须成员过滤）
    std::unordered_map<RDGPassNodeRef, std::vector<RDGPassNodeRef>> successors; // pass到后继pass
    for (RDGPassNodeRef pass : passes)
        successors[pass] = dependency_analysis.get_dependent_by_passes(pass);

    // ---- 权重（快照化的历史耗时）----
    // 快照法保证帧间决策确定——工程品质而非正确性必需：跨帧正确性已由结构保证（created=分槽
    // 池只在N↔N+2复用+Tick帧栅栏挡住+帧末归巢回graphics族；imported=跨帧注册表细链wait上帧
    // per-queue末触点+归巢点prologue承接），队列分配翻转不再有正确性后果。2026-09-12定案
    // "分配无关，罪在隐形依赖边"，旧的"分配稳定性兜底跨帧WAR"叙事已推翻。
    // 同图+同权重⇒ranku序/EFT/tie-break全确定性⇒同调度：可复现调试、QFO链/SSIS点不逐帧抖。
    // 快照每300帧刷新一次（刷新帧存在一次性重排迁移开销）
    static std::unordered_map<std::string, double> s_weightSnapshot;
    static uint32_t s_snapshotAge = 0;
    if (s_snapshotAge == 0)
    {
        for (RDGPassNodeRef pass : passes)
        {
            const std::string name(pass->Name());
            const double live = timingQuery ? timingQuery->GetPassDurationMs(name, config.heft_fallback_duration_ms) : config.heft_fallback_duration_ms;
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
    // 这个 pass 到图终点的最长链有多长
    const std::vector<RDGPassNodeRef>& topoOrder = dependency_analysis.get_logical_topology_result().logical_topological_order;
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
    std::unordered_map<RDGPassNodeRef, uint32_t> assigned;            // 分配好pass的集合，pass到队列
    std::unordered_map<RDGPassNodeRef, uint32_t> remainingPreds;      // pass的入度数
    std::vector<RDGPassNodeRef> ready;                                // 入度为0的pass，就绪集
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
        // 入度=前驱数（依赖表已去重、边只引用本帧pass——直接取size）
        uint32_t cnt = 0;
        if (const PassDependencies* deps = dependency_analysis.get_pass_dependencies(pass))
            cnt = static_cast<uint32_t>(deps->dependent_passes.size());
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
        // 从就绪集中取出到完成链条最长的pass
        for (auto it = ready.begin(); it != ready.end(); ++it)
        {
            const double rNew = ranku[*it], rBest = ranku[*bestIt];
            // 容差 + 名字字典序
            if (rNew > rBest + 1e-9 || (rNew > rBest - 1e-9 && std::string_view((*it)->Name()) < std::string_view((*bestIt)->Name())))
                bestIt = it;
        }
        RDGPassNodeRef v = *bestIt;
        ready.erase(bestIt);

        // 资源访问全集（同资源多条访问合并为单条目，含写即ReadWrite）
        std::vector<std::pair<RDGResourceNodeRef, EResourceAccessType>> accesses;
        if (const PassResourceInfo* info = pass_info_analysis.get_resource_info(v))
        {
            std::unordered_map<RDGResourceNodeRef, EResourceAccessType> merged;     // 资源到资源的访问类型
            // 遍历该pass访问的所有资源
            for (const ResourceAccessInfo& acc : info->resource_accesses)
            {
                if (!acc.resource) continue;
                // CONCURRENT纹理不进所有权表：无QFO往返即无跨族互斥/所有权链——
                // 跳过后下方互斥等待(:335)、所有权迁移(:388)、链回写边(:401)对本资源全部失效
                if (acc.resource->NodeType() == RDG_RESOURCE_NODE_TYPE_TEXTURE &&
                    static_cast<RDGTextureNodeRef>(acc.resource)->GetInfo().sharingMode == RESOURCE_SHARING_TYPE_CONCURRENT) continue;

                EResourceAccessType& slot = merged[acc.resource];
                const bool anyWrite = slot == EResourceAccessType::ReadWrite || acc.access_type != EResourceAccessType::Read;
                slot = anyWrite ? EResourceAccessType::ReadWrite : EResourceAccessType::Read;
            }
            accesses.reserve(merged.size());
            for (const auto& [res, acc] : merged)
                accesses.emplace_back(res, acc);
        }

        const PassDependencies* vDeps = dependency_analysis.get_pass_dependencies(v);
        double bestEft = DBL_MAX;       // pass最早的截至时间
        uint32_t bestQ = 0;             // pass最早截至时间对应的队列
        std::string eftTrace;   // debug决策日志
        std::vector<double> eftOf(queueCount, DBL_MAX);   // 各合格队列候选（滞后判断用）

        // 遍历每一个队列
        for (uint32_t q = 0; q < queueCount; q++)
        {
            if (!is_queue_eligible(v, q)) continue;

            double est = queueReady[q];     // 安排在该队列上的开始时间
            if (vDeps)
            {
                // 遍历该pass的每一个依赖
                for (RDGPassNodeRef pred : vDeps->dependent_passes)
                {
                    const auto fa = assigned.find(pred);
                    if (fa == assigned.end()) continue;          // 未调度前驱（防御——就绪集保证不存在）
                    const double c = (fa->second != q) ? config.heft_comm_cost_ms : 0.0;    // 依赖的pass在另一个队列上
                    est = std::max(est, aft[pred] + c);
                }
            }
            // 所有权约束：访问的资源被异族持有 → 等持有者完成（EXCLUSIVE跨族互斥的排队）RAR
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

            const double eft = est + weight[v]; // 安排在该队列的截至时间
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
        // 抑制EMA波动导致的分配逐帧翻转churn。纯稳定性选择，与正确性无关（created=槽池+
        // 帧栅栏+帧末归巢、imported=注册表细链，分配翻转无正确性后果——2026-09-12
        // "分配无关"定案的延伸）
        static std::unordered_map<std::string, uint32_t> s_lastAssignment;
        const std::string passName(v->Name());
        const auto lastIt = s_lastAssignment.find(passName);
        if (lastIt != s_lastAssignment.end() && lastIt->second != bestQ && 
            lastIt->second < queueCount && eftOf[lastIt->second] < bestEft + config.heft_hysteresis_ms)
        {
            // 若无明显优化不改分配队列
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
    // 遍历每一份资源
    for (const auto& [res, st] : ownership)
    {
        // 该资源的每一次访问
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
        return static_cast<RDGCopyPassNodeRef>(pass)->IsGenerateMip() ? q.supports_graphics : q.supports_copy;
    }
    case RDGPassNodeType::RDG_PASS_NODE_TYPE_COMPUTE:
    {
        if (pass->has_flags(RDGPassFlags::ForceGraphicsQueue))
            return q.supports_graphics;
        return q.supports_compute;
    }
    default:
        return q.supports_graphics;
    }
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