
#include "schedule_reorder.h"
#include "Function/Global/EngineContext.h"
#include <cstdint>

// Constructor
ExecutionReorderPhase::ExecutionReorderPhase(
    const PassInfoAnalysis& pass_info,
    const PassDependencyAnalysis& dependency_analysis,
    const QueueSchedule& timeline,
    const ExecutionReorderConfig& config)
    : config(config)
    , pass_info_analysis(pass_info)
    , dependency_analysis(dependency_analysis)
    , timeline_schedule(timeline)
{}

void ExecutionReorderPhase::reset_for_frame()
{

}

void ExecutionReorderPhase::on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{
    ENGINE_TIME_SCOPE("ExecutionReorderPhase");

    render_graph = graph;
    if (false)
    {
        // Step 1: 从 QueueSchedule 中创建一个时间线副本
        working_timeline = timeline_schedule.get_schedule_result().queue_schedules;

        // Step 2: 运行基于图的优化（不需要重建资源链！）
        run_graph_based_optimization();

        // Step 3: 保存结果
        result.optimized_timeline = std::move(working_timeline);
    }
    else
    {
        result.optimized_timeline = timeline_schedule.get_schedule_result().queue_schedules;
    }
}

// Run graph-based optimization - much simpler!
void ExecutionReorderPhase::run_graph_based_optimization() 
{
    // 每个队列独立优化
    for (size_t i = 0; i < working_timeline.size(); ++i)
    {
        optimize_queue_with_graph(i);
    }
}

// 单队列核心优化 - simplified single-pass algorithm
void ExecutionReorderPhase::optimize_queue_with_graph(uint32_t queue_idx) 
{
    if (working_timeline[queue_idx].size() < 2)
        return;

    std::vector<RDGPassNodeRef>& passes = working_timeline[queue_idx];

    // 单次前向遍历：对每个位置，找最佳候选者移到它后面
    // 避免了复杂的多轮迭代和索引失效问题
    for (size_t current_pos = 0; current_pos < passes.size() - 1; ++current_pos)
    {
        RDGPassNodeRef current_pass = passes[current_pos];

        // 寻找最佳候选者移到 current_pos + 1 位置
        RDGPassNodeRef best_candidate = nullptr;
        size_t best_candidate_pos = SIZE_MAX;
        float best_affinity = config.min_affinity_score;        // 亲和度必须超过此阈值

        // 在吸引力距离范围内扫描后续位置
        size_t max_scan_pos = std::min((uint64_t)passes.size(), (uint64_t)current_pos + 1 + config.max_attraction_distance);
        for (size_t candidate_pos = current_pos + 1; candidate_pos < max_scan_pos; ++candidate_pos)
        {
            RDGPassNodeRef candidate_pass = passes[candidate_pos];

            // 判断：candidate 是否应该被吸引到 current 旁边
            if (should_attract_passes(current_pass, candidate_pass, config.enable_cache_opt, config.enable_lifetime_opt))
            {
                // 安全检查：移动 candidate 是否会违反依赖？
                if (can_attract_pass_safely(queue_idx, current_pos, candidate_pos))
                {
                    // 计算亲和度，选最优候选
                    auto shared_resources = get_shared_resources(current_pass, candidate_pass);
                    float affinity = calculate_resource_affinity_from_shared(current_pass, candidate_pass, shared_resources);

                    if (affinity > best_affinity)
                    {
                        best_candidate = candidate_pass;
                        best_candidate_pos = candidate_pos;
                        best_affinity = affinity;
                    }
                }
            }
        }

        // 找到了好的候选者，且它不在目标位置 → 执行移动
        if (best_candidate && best_candidate_pos != SIZE_MAX && best_candidate_pos != current_pos + 1)
        {
            attract_pass(queue_idx, best_candidate_pos, current_pos + 1);
        }
    }
}

// Graph-based safety check - much simpler than resource chain analysis!
bool ExecutionReorderPhase::can_attract_pass_safely(uint32_t queue_idx, size_t current_pos, size_t target_pos) 
{
    if (current_pos >= target_pos)
        return false;

    const std::vector<RDGPassNodeRef>& passes = working_timeline[queue_idx];

    // 被选中的候选者
    RDGPassNodeRef target_pass = passes[target_pos];

    // 检查：将 target_pass 移到 current_pos+1 是否会违反依赖
    // 只需检查被"跳过"的中间 Pass( 即当前的[current_pos+1，target_pos) )
    for (size_t i = current_pos + 1; i < target_pos; ++i)
    {
        RDGPassNodeRef intermediate_pass = passes[i];

        // 这一点已经由queue_schedule中的pass分配隐性保证
        //// 1. 中间 Pass 不能依赖 target_pass
        //if (has_path_between_passes(target_pass, intermediate_pass))
        //{
        //    return false; // intermediate depends on target_pass, cannot move before it
        //}

        // 2. target_pass 不能依赖 中间pass
        if (has_path_between_passes(intermediate_pass, target_pass))
        {
            return false; // target_pass depends on intermediate, target_pass cannot move before it
        }
    }

    return true;
}

// Unified attraction logic - scans shared resources only once
bool ExecutionReorderPhase::should_attract_passes(RDGPassNodeRef current_pass, RDGPassNodeRef target_pass, bool check_cache, bool check_lifetime) const 
{
    // 两种优化都没开 → 不吸引
    if (!check_cache && !check_lifetime)
        return false;

    // 一次性计算共享资源集合（昂贵操作，只做一次）
    auto shared_resources = get_shared_resources(current_pass, target_pass);

    // 没有共享资源 → 两个 Pass 毫无关系，吸引无意义
    if (shared_resources.empty())
        return false;

    bool should_attract_for_lifetime = false;
    bool should_attract_for_cache = false;

    // 生命周期优化：只要共享资源就值得拉近
    // 因为 producer→consumer 越近，资源存活时间越短，峰值显存越低
    if (check_lifetime)
    {
        should_attract_for_lifetime = true;
    }

    // 缓存优化：亲和度必须超过阈值
    if (check_cache)
    {
        float affinity = calculate_resource_affinity_from_shared(current_pass, target_pass, shared_resources);
        should_attract_for_cache = (affinity >= config.min_affinity_score);
    }

    // 任一优化认为值得吸引 → 吸引
    return should_attract_for_lifetime || should_attract_for_cache;
}

// Graph traversal: check if there's a directed path from from_pass to to_pass
// 即to_pass 依赖于 from_pass
bool ExecutionReorderPhase::has_path_between_passes(RDGPassNodeRef from_pass, RDGPassNodeRef to_pass) const 
{
    if (!from_pass || !to_pass) return false;
    if (from_pass == to_pass) return false; // 不考虑自环

    // 先查直接依赖
    const PassDependencies* deps = dependency_analysis.get_pass_dependencies(to_pass);
    if (!deps) return false;

    if (deps->has_dependency_on(from_pass)) return true;

    // 直接依赖没找到 → BFS 查传递依赖
    std::unordered_set<RDGPassNodeRef> visited;
    std::vector<RDGPassNodeRef> queue;

    // 初始：to_pass 的直接依赖入队
    for (auto* dep_pass : deps->dependent_passes)
    {
        if (dep_pass == from_pass) return true;
        auto [it, inserted] = visited.insert(dep_pass);
        if (inserted)
        {
            queue.push_back(dep_pass);
        }
    }

    // BFS 沿依赖链向上追溯
    for (size_t i = 0; i < queue.size(); ++i)
    {
        RDGPassNodeRef current = queue[i];
        const auto* current_deps = dependency_analysis.get_pass_dependencies(current);
        if (!current_deps) continue;

        for (auto* next_dep : current_deps->dependent_passes)
        {
            // 找到了
            if (next_dep == from_pass) return true;

            auto [it, inserted] = visited.insert(next_dep);
            if (inserted)
            {
                queue.push_back(next_dep);
            }
        }
    }

    // 遍历完所有传递依赖，未找到 from_pass
    return false;
}

// 共享资源查询
std::vector<RDGResourceNodeRef> ExecutionReorderPhase::get_shared_resources(RDGPassNodeRef pass1, RDGPassNodeRef pass2) const 
{
    std::vector<RDGResourceNodeRef> shared;
    std::unordered_set<RDGResourceNodeRef> resources1;

    // 从 PassInfoAnalysis 获取 Pass 的资源访问列表
    const auto* info1 = pass_info_analysis.get_pass_info(pass1);
    const auto* info2 = pass_info_analysis.get_pass_info(pass2);

    // 将 pass1 的所有资源加入 hash set
    for (const auto& access : info1->resource_info.resource_accesses)
    {
        resources1.insert(access.resource);
    }

    // 遍历 pass2 的资源，在 hash set 中查找交集
    for (const auto& access : info2->resource_info.resource_accesses)
    {
        if (resources1.contains(access.resource))
        {
            shared.push_back(access.resource);
        }
    }

    return shared;
}

// 执行移动 (move pass from from_pos to to_pos)
void ExecutionReorderPhase::attract_pass(uint32_t queue_idx, size_t from_pos, size_t to_pos) 
{
    auto& passes = working_timeline[queue_idx];

    // 索引合法性检查
    if (from_pos >= passes.size() || to_pos >= passes.size() || from_pos == to_pos)
        return;

    // 保存要移动的 Pass，从原位置删除
    RDGPassNodeRef pass_to_move = passes[from_pos];
    passes.erase(passes.begin() + from_pos);

      // 因为 from_pos > to_pos（从后面移到前面），
      // 删除 from_pos 后 to_pos 及之前的元素位置不变，无需调整
    passes.insert(passes.begin() + to_pos, pass_to_move);

    // No need to update complex data structures - we use the graph directly!
}

float ExecutionReorderPhase::calculate_resource_affinity_from_shared(RDGPassNodeRef pass1, RDGPassNodeRef pass2, const std::vector<RDGResourceNodeRef>& shared_resources) const 
{
    const auto* info1 = pass_info_analysis.get_pass_info(pass1);
    const auto* info2 = pass_info_analysis.get_pass_info(pass2);

    if (!info1 || !info2) return 0.0f;

    uint32_t shared_count = static_cast<uint32_t>(shared_resources.size());
    uint32_t total_resources = info1->resource_info.total_resource_count + info2->resource_info.total_resource_count;

    if (total_resources == 0) return 0.0f;

    // Jaccard 相似度: |A∩B| / |A∪B|
    // |A∪B| = |A| + |B| - |A∩B|
    uint32_t union_size = total_resources - shared_count;
    if (union_size == 0) return 0.0f;

    return static_cast<float>(shared_count) / static_cast<float>(union_size);
}
