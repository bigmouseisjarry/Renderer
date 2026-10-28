
#include <algorithm>

#include "pass_dependency_analysis.h"
#include "pass_info_analysis.h"
#include "Function/Global/EngineContext.h"
#include "queue_schedule.h"

PassDependencyAnalysis::PassDependencyAnalysis(const PassInfoAnalysis& pass_info_analysis)
    : pass_info_analysis(pass_info_analysis)
{
}

void PassDependencyAnalysis::reset_for_frame()
{
    pass_dependencies_.clear();      // 保留 bucket，下帧复用
    logical_topology_.logical_topological_order.clear();
    logical_topology_.logical_levels.clear();
    logical_topology_.logical_critical_path.clear();
    logical_topology_.max_logical_dependency_depth = 0;
}

bool PassDependencies::has_dependency_on(RDGPassNodeRef pass) const
{
    for (const auto& dep : resource_dependencies)
    {
        if (dep.dependent_pass == pass)
            return true;
    }
    return false;
}

void PassDependencyAnalysis::on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{
    ENGINE_TIME_SCOPE(PassDependencyAnalysis::on_execute);

    // 建依赖边
    analyze_pass_dependencies(graph);
    // Kahn拓扑排序（唯一产出logical_topological_order——HEFT ranku的输入）
    perform_logical_topological_sort_optimized();
}

void PassDependencyAnalysis::analyze_pass_dependencies(RDGDependencyGraphRef graph)
{
    ENGINE_TIME_SCOPE(PassDependencyAnalysis::analyze_pass_dependencies);

    std::vector<RDGPassNodeRef>& all_passes = get_passes(graph);

    // 为每个资源维护最后访问的Pass和访问信息
    std::unordered_map<RDGResourceNodeRef, LastResourceAccess> resource_last_access_;
    resource_last_access_.reserve(graph->ResourceNodeCount()); // 预分配避免rehash

    // 按创建顺序遍历每一个pass
    for (RDGPassNodeRef current_pass : all_passes)
    {
        PassDependencies& current_deps = pass_dependencies_[current_pass];

        // 从 PassInfoAnalysis 阶段获取到计算后的每一个pass的资源访问信息
        const PassResourceInfo* current_resource_info = pass_info_analysis.get_resource_info(current_pass);
        if (!current_resource_info)
            continue;

        // 遍历这个pass访问的每一个资源
        for (const auto& current_access : current_resource_info->resource_accesses)
        {
            RDGResourceNodeRef resource = current_access.resource;
            auto& last_access = resource_last_access_[resource];

            const bool pureRead = current_access.access_type == EResourceAccessType::Read;
            if (last_access.last_pass != nullptr && last_access.last_pass != current_pass)
            {
                ResourceDependency dep;
                dep.dependent_pass = last_access.last_pass;
                dep.resource = resource;
                dep.current_access = current_access.access_type;
                dep.current_state = current_access.resource_state;
                dep.previous_access = last_access.last_access_type;
                dep.previous_state = last_access.last_state;
                current_deps.resource_dependencies.push_back(dep);

                // 加入当前pass的依赖信息
                if (std::find(current_deps.dependent_passes.begin(), current_deps.dependent_passes.end(), dep.dependent_pass) == current_deps.dependent_passes.end())
                    current_deps.dependent_passes.push_back(dep.dependent_pass);
            }

            if (!pureRead)
            {
                // WAR：写者（含ReadWrite）须等本帧自最近写者以来的全体读者读完
                for (const auto& [reader, readerState] : last_access.readers_since_write)
                {
                    if (reader == current_pass) continue;
                    if (std::find(current_deps.dependent_passes.begin(), current_deps.dependent_passes.end(), reader) != current_deps.dependent_passes.end())
                        continue;   // 该读者已有pass级依赖（多资源同对），跳过重复边

                    ResourceDependency dep;
                    dep.dependent_pass = reader;
                    dep.resource = resource;
                    dep.current_access = current_access.access_type;
                    dep.previous_access = EResourceAccessType::Read;
                    dep.current_state = current_access.resource_state;
                    dep.previous_state = readerState;

                    // 加入当前pass的依赖信息
                    current_deps.resource_dependencies.push_back(dep);
                    current_deps.dependent_passes.push_back(reader);
                }
            }

            // 更新锚与读者登记——纯读者不推进last_pass锚（写者锚定），但登记自己；
            // 写者推进锚并清空读者登记（后续读者改锚定新写者）
            if (pureRead)
            {
                if (!std::ranges::any_of(last_access.readers_since_write, [&](const auto& entry) {return entry.first == current_pass; }))
                    last_access.readers_since_write.emplace_back(current_pass, current_access.resource_state);
            }
            else
            {
                last_access.last_pass = current_pass;
                last_access.last_access_type = current_access.access_type;
                last_access.last_state = current_access.resource_state;
                last_access.readers_since_write.clear();
            }
        }
    }

    // 构建反向依赖，即（谁依赖了我）
    {
        ENGINE_TIME_SCOPE("DoInsert");
        for (auto& [pass, deps] : pass_dependencies_)
        {
            for (RDGPassNodeRef dep_pass : deps.dependent_passes)
            {
                pass_dependencies_[dep_pass].dependent_by_passes.push_back(pass);
            }
        }
    }
}

void PassDependencyAnalysis::perform_logical_topological_sort_optimized()
{
    ENGINE_TIME_SCOPE("PerformLogicalTopologicalSortOptimized");

    if (pass_dependencies_.empty()) return;

    // Kahn拓扑排序——唯一产出logical_topological_order（HEFT ranku的逆序DP基准，
    // 调度后由apply_schedule_order重建为调度序）。级别分组/关键路径/逐pass索引产出
    // 已随消费者清退删除（2026-10-24瘦身，消费者仅剩HEFT与barrier的拓扑序）
    std::unordered_map<RDGPassNodeRef, uint32_t> inDegrees;
    inDegrees.reserve(pass_dependencies_.size());
    std::vector<RDGPassNodeRef> queue;
    queue.reserve(pass_dependencies_.size());
    logical_topology_.logical_topological_order.reserve(pass_dependencies_.size());

    for (auto& [pass, deps] : pass_dependencies_)
    {
        const uint32_t degree = static_cast<uint32_t>(deps.dependent_passes.size());
        inDegrees[pass] = degree;
        if (degree == 0) queue.push_back(pass);
    }

    for (size_t i = 0; i < queue.size(); ++i)
    {
        RDGPassNodeRef current = queue[i];
        logical_topology_.logical_topological_order.push_back(current);

        if (auto it = pass_dependencies_.find(current); it != pass_dependencies_.end())
            for (RDGPassNodeRef dependent : it->second.dependent_by_passes)
                if (--inDegrees[dependent] == 0)
                    queue.push_back(dependent);
    }
}

void PassDependencyAnalysis::identify_logical_critical_path()
{
    ENGINE_TIME_SCOPE("IdentifyLogicalCriticalPath");

    // Step 1: 计算每个节点的高度（到DAG末尾的最长距离）
    // 按逆拓扑序处理，确保处理节点时其所有后继都已处理
    for (int i = logical_topology_.logical_topological_order.size() - 1; i >= 0; --i)
    {
        RDGPassNode* pass = logical_topology_.logical_topological_order[i];
        auto pass_it = pass_dependencies_.find(pass);
        if (pass_it == pass_dependencies_.end())
            continue; // 如果没有依赖信息，跳过

        // 如果没有后继节点，高度为0
        if (pass_it->second.dependent_by_passes.empty())
        {
            pass_it->second.logical_critical_path_length = 0;
        }
        else
        {
            // 高度 = 1 + 所有后继节点的最大高度
            uint32_t max_height = 0;
            for (auto* dependent : pass_it->second.dependent_by_passes)
            {
                if (auto dep_it = pass_dependencies_.find(dependent);
                    dep_it != pass_dependencies_.end())
                {
                    max_height = std::max(max_height, dep_it->second.logical_critical_path_length);
                }
            }
            pass_it->second.logical_critical_path_length = 1 + max_height;
        }
    }

    // Step 2: 找到高度最大的起始节点（没有前驱的节点）
    RDGPassNodeRef critical_start = nullptr;
    uint32_t max_height = 0;

    for (const auto& [pass, deps] : pass_dependencies_)
    {
        // 起始节点：没有依赖其他Pass
        if (deps.dependent_passes.empty() && deps.logical_critical_path_length > max_height)
        {
            max_height = deps.logical_critical_path_length;
            critical_start = pass;
        }
    }

    // Step 3: 沿着高度递减的路径追踪关键路径
    if (critical_start)
    {
        RDGPassNodeRef current = critical_start;

        while (current)
        {
            logical_topology_.logical_critical_path.push_back(current);

            // 在所有后继中选择高度最大的
            RDGPassNodeRef next = nullptr;
            uint32_t next_height = 0;

            if (auto it = pass_dependencies_.find(current); it != pass_dependencies_.end())
            {
                for (auto* dependent : it->second.dependent_by_passes)
                {
                    if (auto dep_it = pass_dependencies_.find(dependent);dep_it!=pass_dependencies_.end())
                    {
                        // 选择高度最大的后继（如果有多个相同高度的，选择第一个）
                        if (dep_it->second.logical_critical_path_length > next_height ||
                            (dep_it->second.logical_critical_path_length == next_height && !next))
                        {
                            next_height = dep_it->second.logical_critical_path_length;
                            next = dependent;
                        }
                    }
                }
            }

            current = next;
        }
    }
}

uint32_t PassDependencyAnalysis::get_dependency_level(RDGPassNodeRef pass) const
{
    auto it = pass_dependencies_.find(pass);
    return it != pass_dependencies_.end() ? it->second.logical_dependency_level : 0;
}

uint32_t PassDependencyAnalysis::get_topological_order(RDGPassNodeRef pass) const
{
    auto it = pass_dependencies_.find(pass);
    return it != pass_dependencies_.end() ? it->second.logical_topological_order : UINT32_MAX;
}

uint32_t PassDependencyAnalysis::get_logical_critical_path_length(RDGPassNodeRef pass) const
{
    auto it = pass_dependencies_.find(pass);
    return it != pass_dependencies_.end() ? it->second.logical_critical_path_length : 0;
}

bool PassDependencyAnalysis::can_execute_in_parallel_logically(RDGPassNodeRef pass1, RDGPassNodeRef pass2) const
{
    if (!pass1 || !pass2 || pass1 == pass2) return false;

    auto it1 = pass_dependencies_.find(pass1);
    auto it2 = pass_dependencies_.find(pass2);

    if (it1 == pass_dependencies_.end() || it2 == pass_dependencies_.end()) return false;

    // 同 level 且无直接依赖
    if (it1->second.logical_dependency_level != it2->second.logical_dependency_level) return false;

    // Check if pass1 depends on pass2
    for (auto* dep : it1->second.dependent_passes)
    {
        if (dep == pass2) return false;
    }

    // Check if pass2 depends on pass1
    for (auto* dep : it2->second.dependent_passes)
    {
        if (dep == pass1) return false;
    }

    return true;
}

const PassDependencies* PassDependencyAnalysis::get_pass_dependencies(RDGPassNodeRef pass) const
{
    auto it = pass_dependencies_.find(pass);
    return it!=pass_dependencies_.end() ? &it->second : nullptr;
}

bool PassDependencyAnalysis::has_dependencies(RDGPassNodeRef pass) const
{
    const auto* deps = get_pass_dependencies(pass);
    return deps != nullptr && !deps->resource_dependencies.empty();
}

// For ScheduleTimeline - get pass-level dependencies directly
const std::vector<RDGPassNodeRef>& PassDependencyAnalysis::get_dependent_passes(RDGPassNodeRef pass) const
{
    static const std::vector<RDGPassNodeRef> empty_vector;
    const auto* deps = get_pass_dependencies(pass);
    return deps ? deps->dependent_passes : empty_vector;
}

const std::vector<RDGPassNodeRef>& PassDependencyAnalysis::get_dependent_by_passes(RDGPassNodeRef pass) const
{
    static const std::vector<RDGPassNodeRef> empty_vector;
    const auto* deps = get_pass_dependencies(pass);
    return deps ? deps->dependent_by_passes : empty_vector;
}

void PassDependencyAnalysis::dump_dependencies() const
{
    ENGINE_LOG_INFO("========== Pass Dependency Analysis Results ==========");

    for (const auto& [pass, deps] : pass_dependencies_)
    {
        if (deps.resource_dependencies.empty())
            continue;

        ENGINE_LOG_INFO("Pass: {} depends on:", pass->Name());

        for (const auto& dep : deps.resource_dependencies)
        {
            const char* dep_type_str = "Unknown";
            const char* current_access_str = "Unknown";
            const char* previous_access_str = "Unknown";

            switch (dep.current_access)
            {
            case EResourceAccessType::Read:
                current_access_str = "Read";
                break;
            case EResourceAccessType::Write:
                current_access_str = "Write";
                break;
            case EResourceAccessType::ReadWrite:
                current_access_str = "ReadWrite";
                break;
            }

            switch (dep.previous_access)
            {
            case EResourceAccessType::Read:
                previous_access_str = "Read";
                break;
            case EResourceAccessType::Write:
                previous_access_str = "Write";
                break;
            case EResourceAccessType::ReadWrite:
                previous_access_str = "ReadWrite";
                break;
            }

            ENGINE_LOG_INFO("  -> Pass: {}, Resource: {}, {}->{}, State: {}->{}",
                dep.dependent_pass->Name(),
                dep.resource->Name(),
                previous_access_str,
                current_access_str,
                (int)dep.previous_state,
                (int)dep.current_state);
        }
    }

    ENGINE_LOG_INFO("==========================================");
}

void PassDependencyAnalysis::dump_logical_topology() const
{
    ENGINE_LOG_INFO("========== Logical Topology Analysis ==========");
    ENGINE_LOG_INFO("Max logical dependency depth: {}", logical_topology_.max_logical_dependency_depth);
    ENGINE_LOG_INFO("Logical topological order:");

    for (size_t i = 0; i < logical_topology_.logical_topological_order.size(); ++i)
    {
        auto* pass = logical_topology_.logical_topological_order[i];
        uint32_t level = get_dependency_level(pass);

        ENGINE_LOG_INFO("  [{}] {} (logical level: {})", i, pass->Name(), level);
    }

    ENGINE_LOG_INFO("\nLogical dependency levels:");
    for (const auto& level : logical_topology_.logical_levels)
    {
        ENGINE_LOG_INFO("Level {} ({} passes):",
            level.level,
            static_cast<uint32_t>(level.passes.size()));

        for (auto* pass : level.passes)
        {
            uint32_t critical_length = get_logical_critical_path_length(pass);
            ENGINE_LOG_INFO("  - {} (critical path length: {})",
                pass->Name(),
                critical_length);
        }
    }

    ENGINE_LOG_INFO("=============================================");
}

void PassDependencyAnalysis::dump_logical_critical_path() const
{
    ENGINE_LOG_INFO("========== Logical Critical Path ==========");
    ENGINE_LOG_INFO("Logical critical path length: {}", static_cast<uint32_t>(logical_topology_.logical_critical_path.size()));

    for (size_t i = 0; i < logical_topology_.logical_critical_path.size(); ++i)
    {
        auto* pass = logical_topology_.logical_critical_path[i];
        uint32_t level = get_dependency_level(pass);

        ENGINE_LOG_INFO("[{}] {} (logical level: {})",
            i,
            pass->Name(),
            level);
    }

    ENGINE_LOG_INFO("=========================================");
}


// ---- HEFT回写接口 ///////////////////////////////////////////////////////////////////////

void PassDependencyAnalysis::add_scheduling_order_edges(const std::vector<SchedulingOrderEdge>& edges)
{
    // 所有权链序边追加（幂等：pass对+资源三元组去重）。边的状态对从pass_info_analysis的实际
    // 访问取（HEFT只管访问序不管状态——SSIS点的wait stage推导与Phase 7的屏障语义需要真实状态）
    for (const auto& edge : edges)
    {
        if (!edge.from || !edge.to || edge.from == edge.to || !edge.resource) continue;

        PassDependencies& deps = pass_dependencies_[edge.to];

        // 幂等：同pass对+同资源已建边则跳过
        bool exists = false;
        for (const auto& dep : deps.resource_dependencies)
            if (dep.dependent_pass == edge.from && dep.resource == edge.resource) { exists = true; break; }
        if (exists) continue;

        ResourceDependency dep;
        dep.dependent_pass = edge.from;
        dep.resource = edge.resource;
        dep.current_access = edge.toAccess;
        dep.previous_access = edge.fromAccess;
        dep.current_state = pass_info_analysis.get_resource_state(edge.to, edge.resource);
        dep.previous_state = pass_info_analysis.get_resource_state(edge.from, edge.resource);
        deps.resource_dependencies.push_back(dep);

        // 双向表同步维护（Kahn入度=前向dependent_passes计数、递减遍历反向dependent_by_passes
        if (std::find(deps.dependent_passes.begin(), deps.dependent_passes.end(), edge.from) == deps.dependent_passes.end())
            deps.dependent_passes.push_back(edge.from);
        PassDependencies& fromDeps = pass_dependencies_[edge.from];
        if (std::find(fromDeps.dependent_by_passes.begin(), fromDeps.dependent_by_passes.end(), edge.to) == fromDeps.dependent_by_passes.end())
            fromDeps.dependent_by_passes.push_back(edge.to);
    }
}

void PassDependencyAnalysis::apply_schedule_order(const std::vector<RDGPassNodeRef>& order)
{
    // 按HEFT调度序直接重建拓扑（合法序：就绪集保证前驱先于后继调度）。级别=前驱最大级别+1
    // 按序一次遍历（前驱必已计算）。关键路径照旧（高度DP按dependent_by_passes，遍历序无关）。
    // 本函数是级别分组/关键路径/逐pass索引的唯一权威生产点——预排序只产出拓扑序（ranku输入），
    // 这些字段调度前不存在（2026-10-24瘦身定案）
    logical_topology_.logical_topological_order.clear();
    logical_topology_.logical_levels.clear();
    logical_topology_.logical_critical_path.clear();
    logical_topology_.max_logical_dependency_depth = 0;

    uint32_t maxLevel = 0;
    for (RDGPassNodeRef pass : order)
    {
        auto found = pass_dependencies_.find(pass);
        if (found == pass_dependencies_.end()) continue;
        PassDependencies& deps = found->second;

        uint32_t level = 0;
        for (RDGPassNodeRef pred : deps.dependent_passes)
        {
            auto predFound = pass_dependencies_.find(pred);
            if (predFound != pass_dependencies_.end())
                level = std::max(level, predFound->second.logical_dependency_level + 1);
        }
        deps.logical_dependency_level = level;
        deps.logical_topological_order = static_cast<uint32_t>(logical_topology_.logical_topological_order.size());
        logical_topology_.logical_topological_order.push_back(pass);
        maxLevel = std::max(maxLevel, level);
    }

    logical_topology_.max_logical_dependency_depth = maxLevel;
    logical_topology_.logical_levels.resize(maxLevel + 1);
    for (uint32_t i = 0; i <= maxLevel; ++i)
    {
        logical_topology_.logical_levels[i].level = i;
        logical_topology_.logical_levels[i].passes.clear();
    }
    for (RDGPassNodeRef pass : logical_topology_.logical_topological_order)
        logical_topology_.logical_levels[pass_dependencies_[pass].logical_dependency_level].passes.push_back(pass);

    identify_logical_critical_path();
}
