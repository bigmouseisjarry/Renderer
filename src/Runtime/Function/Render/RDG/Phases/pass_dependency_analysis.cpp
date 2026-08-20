
#include "pass_dependency_analysis.h"
#include "pass_info_analysis.h"
#include "Function/Global/EngineContext.h"
#include "queue_schedule.h"
//#include "SkrRenderGraph/phases_v2/cross_queue_sync_analysis.hpp"

PassDependencyAnalysis::PassDependencyAnalysis(const PassInfoAnalysis& pass_info_analysis)
    : pass_info_analysis(pass_info_analysis)
{
    in_degrees_.reserve(64);
    topo_queue_.reserve(64);
    topo_levels_.reserve(64);
}

void PassDependencyAnalysis::reset_for_frame()
{
    pass_dependencies_.clear();      // 保留 bucket，下帧复用
    in_degrees_.clear();
    topo_queue_.clear();             
    topo_levels_.clear();
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

// IRenderGraphPhase 接口实现
void PassDependencyAnalysis::on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{
    ENGINE_TIME_SCOPE(PassDependencyAnalysis::on_execute);

    // 建依赖边
    analyze_pass_dependencies(graph);
    // 拓扑排序+分级
    perform_logical_topological_sort_optimized();
    // 找关键路径
    identify_logical_critical_path();
}

void PassDependencyAnalysis::analyze_pass_dependencies(RDGDependencyGraphRef graph)
{
    ENGINE_TIME_SCOPE(PassDependencyAnalysis::analyze_pass_dependencies);

    // 优化版本：O(n) 复杂度，为每个资源维护最后访问者索引
    auto& all_passes = get_passes(graph);

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

            // 如果这个资源之前被访问过，创建两个pass间的依赖
            // 这里会为 last_pass---read---->resource ----read---->current_pass间也创建依赖
            if (last_access.last_pass != nullptr && last_access.last_pass != current_pass)
            {
                ResourceDependency dep;
                dep.dependent_pass = last_access.last_pass;
                dep.resource = resource;
                dep.current_access = current_access.access_type;
                dep.previous_access = last_access.last_access_type;
                dep.current_state = current_access.resource_state;
                dep.previous_state = last_access.last_state;
                current_deps.resource_dependencies.push_back(dep);

                // 唯一性的插入
                if (std::find(current_deps.dependent_passes.begin(), current_deps.dependent_passes.end(), dep.dependent_pass) == current_deps.dependent_passes.end())
                    current_deps.dependent_passes.push_back(dep.dependent_pass);
            }

            // 更新该资源的最后一次访问信息
            last_access.last_pass = current_pass;
            last_access.last_access_type = current_access.access_type;
            last_access.last_state = current_access.resource_state;
        }
    }

    // Build dependent_by_passes (reverse dependencies)
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

    const size_t num_passes = pass_dependencies_.size();
    if (num_passes == 0) return;

    // 预分配容器，避免动态扩容
    in_degrees_.reserve(num_passes);
    topo_queue_.reserve(num_passes);
    topo_levels_.reserve(num_passes);
    logical_topology_.logical_topological_order.reserve(num_passes);
    logical_topology_.logical_levels.reserve(num_passes);

    // 第一步：计算所有节点的入度，同时初始化依赖级别为0
    for (auto& [pass, deps] : pass_dependencies_)
    {
        in_degrees_[pass] = static_cast<uint32_t>(deps.dependent_passes.size());
        deps.logical_dependency_level = 0;           // 初始化级别
        deps.logical_topological_order = UINT32_MAX; // 临时标记为未处理
    }

    // 第二步：找到所有入度为0的节点（没有依赖的节点）
    for (const auto& [pass, degree] : in_degrees_)
    {
        if (degree == 0)
        {
            topo_queue_.push_back(pass);
            topo_levels_.push_back(0); // 起始节点的级别为0
        }
    }

    // 第三步：Kahn算法 + 同时计算依赖级别
    size_t queue_idx = 0;
    while (queue_idx < topo_queue_.size())
    {
        RDGPassNodeRef current = topo_queue_[queue_idx];
        uint32_t current_level = topo_levels_[queue_idx];
        ++queue_idx;

        // 添加到拓扑排序结果
        logical_topology_.logical_topological_order.push_back(current);

        // 设置当前节点的拓扑索引和依赖级别
        if (auto current_it = pass_dependencies_.find(current);current_it != pass_dependencies_.end())
        {
            current_it->second.logical_topological_order = static_cast<uint32_t>(logical_topology_.logical_topological_order.size() - 1);
            current_it->second.logical_dependency_level = current_level;

            // 处理所有依赖于当前节点的节点
            for (auto* dependent : current_it->second.dependent_by_passes)
            {
                // 减少入度（即这个节点的依赖减一）
                --in_degrees_[dependent];

                // 更新依赖节点的级别（取所有前驱的最大级别+1）
                if (auto dep_it = pass_dependencies_.find(dependent); dep_it != pass_dependencies_.end())
                {
                    dep_it->second.logical_dependency_level = std::max(
                        dep_it->second.logical_dependency_level,
                        current_level + 1);

                    // 如果入度变为0，加入队列（复用已找到的iterator）(前置依赖都已经完成)
                    if (in_degrees_[dependent] == 0)
                    {
                        topo_queue_.push_back(dependent);
                        topo_levels_.push_back(dep_it->second.logical_dependency_level);
                    }
                }
                else if (in_degrees_[dependent] == 0)
                {
                    // 备用路径，正常情况下不应该执行到这里
                    topo_queue_.push_back(dependent);
                    topo_levels_.push_back(0);
                }
            }
        }
    }

    // 第五步：计算最大依赖深度并构建级别分组
    logical_topology_.max_logical_dependency_depth = 0;
    for (const auto& [pass, deps] : pass_dependencies_)
    {
        logical_topology_.max_logical_dependency_depth = std::max(
            logical_topology_.max_logical_dependency_depth,
            deps.logical_dependency_level);
    }

    // 构建级别分组
    logical_topology_.logical_levels.resize(logical_topology_.max_logical_dependency_depth + 1);

    for (uint32_t i = 0; i <= logical_topology_.max_logical_dependency_depth; ++i)
    {
        logical_topology_.logical_levels[i].level = i;
        logical_topology_.logical_levels[i].passes.clear();
    }

    // 将节点分配到对应级别
    for (const auto& [pass, deps] : pass_dependencies_)
    {
        logical_topology_.logical_levels[deps.logical_dependency_level].passes.push_back(pass);
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

// 跨队列同步点生成方法
void PassDependencyAnalysis::generate_cross_queue_sync_points(const QueueSchedule& queue_schedule, std::vector<CrossQueueSyncPoint>& sync_points) const
{
    //const auto& queue_result = queue_schedule.get_schedule_result();
    //sync_points.clear();

    //// 为每个Pass检查其依赖关系
    //for (const auto& [consumer_pass, deps] : pass_dependencies_)
    //{
    //    uint32_t consumer_queue = queue_result.pass_queue_assignments.find(consumer_pass).value();

    //    // 检查每个资源依赖
    //    for (const auto& resource_dep : deps.resource_dependencies)
    //    {
    //        RDGPassNodeRef producer_pass = resource_dep.dependent_pass;
    //        uint32_t producer_queue = queue_result.pass_queue_assignments.find(producer_pass).value();

    //        // 如果生产者和消费者在不同队列上，创建跨队列同步点
    //        if (producer_queue != consumer_queue)
    //        {
    //            CrossQueueSyncPoint sync_point;
    //            sync_point.type = ESyncPointType::Signal;
    //            sync_point.producer_queue_index = producer_queue;
    //            sync_point.consumer_queue_index = consumer_queue;
    //            sync_point.producer_pass = producer_pass;
    //            sync_point.consumer_pass = consumer_pass;
    //            sync_point.resource = resource_dep.resource;
    //            sync_point.from_state = resource_dep.previous_state;
    //            sync_point.to_state = resource_dep.current_state;
    //            sync_point.sync_value = 0;

    //            sync_points.add(sync_point);
    //        }
    //    }
    //}
}
