#include "cross_queue_sync_analysis.h"
#include "pass_info_analysis.h"
#include "Function/Global/EngineContext.h"
#include <algorithm>


CrossQueueSyncAnalysis::CrossQueueSyncAnalysis(
    const PassDependencyAnalysis& dependency_analysis,
    const QueueSchedule& queue_schedule,
    const CrossQueueSyncConfig& config)
    : config_(config)
    , dependency_analysis_(dependency_analysis)
    , queue_schedule_(queue_schedule)
{}

void CrossQueueSyncAnalysis::reset_for_frame()
{
    ssis_result_ = {};
    pass_ssis_.clear();
    pass_local_to_queue_indices_.clear();
    pass_nodes_to_sync_with_.clear();
    total_queue_count_ = 0;
}

void CrossQueueSyncAnalysis::on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{
    //ENGINE_LOG_INFO("CrossQueueSyncAnalysis");
    //ENGINE_LOG_INFO("CrossQueueSyncAnalysis: Starting SSIS analysis");

    // 构建Pass到队列的映射缓存
    const TimelineScheduleResult& queue_result = queue_schedule_.get_schedule_result();

    // Step 1: 从 PassDependencyAnalysis 生成原始同步点
    // 每一条跨队列的资源依赖边都会产生一个
    dependency_analysis_.generate_cross_queue_sync_points(queue_schedule_, ssis_result_.raw_sync_points);
    ssis_result_.total_raw_syncs = static_cast<uint32_t>(ssis_result_.raw_sync_points.size());
    //ENGINE_LOG_INFO("CrossQueueSyncAnalysis: Received {} raw sync points from PassDependencyAnalysis", ssis_result_.total_raw_syncs);

    // Step 3: 应用SSIS优化算法
    if (config_.enable_ssis_optimization)
    {
        apply_ssis_optimization(graph);
    }
    else
    {
        // 如果不启用SSIS，直接复制原始同步点
        ssis_result_.optimized_sync_points = ssis_result_.raw_sync_points;
    }

    // Step 5: 计算优化统计信息
    calculate_optimization_statistics();

    // 可选：输出调试信息
    if (config_.enable_debug_output)
    {
        dump_ssis_analysis();
    }

    //ENGINE_LOG_INFO("CrossQueueSyncAnalysis: SSIS analysis completed - reduced {} sync points to {} ({} reduction)",
    //    ssis_result_.total_raw_syncs,
    //    ssis_result_.total_optimized_syncs,
    //    ssis_result_.optimization_ratio * 100.0f);
}


// 每个 Pass 维护一个数组 ssis[queue_count]，其中 ssis[q] 表示"该 Pass需要与队列 q 在位置 ssis[q] 处同步"
void CrossQueueSyncAnalysis::apply_ssis_optimization(RDGDependencyGraphRef graph)
{
    //ENGINE_LOG_INFO("CrossQueueSyncAnalysis: Applying SSIS optimization algorithm");

    const TimelineScheduleResult& queue_result = queue_schedule_.get_schedule_result();
    total_queue_count_ = static_cast<uint32_t>(queue_result.queue_schedules.size());

    // Step 1: 初始化SSIS值和本地队列索引
    // 对每个 Pass，初始化 SSIS 数组（大小 = 队列总数，全填 InvalidSyncIndex）
    for (auto* pass : get_passes(graph))
    {
        // 初始化SSIS为InvalidSyncIndex
        std::vector<uint32_t>& ssis = pass_ssis_[pass];
        ssis.resize(total_queue_count_, InvalidSyncIndex);

        // 初始化nodes_to_sync_with为空
        pass_nodes_to_sync_with_[pass].clear();
    }

    // 设置每个Pass的本地队列索引
    for (uint32_t queue_idx = 0; queue_idx < queue_result.queue_schedules.size(); ++queue_idx)
    {
        const std::vector<RDGPassNodeRef>& queue_schedule = queue_result.queue_schedules[queue_idx];
        for (uint32_t local_idx = 0; local_idx < queue_schedule.size(); ++local_idx)
        {
            RDGPassNodeRef pass = queue_schedule[local_idx];
            pass_local_to_queue_indices_[pass] = local_idx;
        }
    }

    // Step 2: 第一阶段 - 构建初始SSIS值
    // 从原始同步点构建需要同步的节点列表
    // 收集每个 consumer Pass 的所有跨队列依赖 producer。一个 consumer 可能依赖多个producer，但它们可能在同一个队列上
    for (const auto& sync_point : ssis_result_.raw_sync_points)
    {
        RDGPassNodeRef consumer = sync_point.consumer_pass;
        RDGPassNodeRef producer = sync_point.producer_pass;

        // 添加到需要同步的节点列表
        std::vector<RDGPassNodeRef>& nodes_to_sync = pass_nodes_to_sync_with_[consumer];

        // 避免重复添加
        bool already_exists = false;
        for (auto* node : nodes_to_sync)
        {
            if (node == producer)
            {
                already_exists = true;
                break;
            }
        }
        if (!already_exists)
        {
            nodes_to_sync.push_back(producer);
        }
    }

    // 为每个Pass计算初始SSIS
    for (auto* pass : get_passes(graph))
    {
        uint32_t pass_queue = get_pass_queue_index(pass);
        std::vector<uint32_t>& ssis = pass_ssis_[pass];
        std::vector<RDGPassNodeRef>& nodes_to_sync = pass_nodes_to_sync_with_[pass];

        // 找到每个队列上最近的依赖节点
        std::vector<RDGPassNodeRef> closest_nodes_per_queue;
        closest_nodes_per_queue.resize(total_queue_count_, nullptr);

        for (RDGPassNodeRef dep_node : nodes_to_sync)
        {
            uint32_t dep_queue = get_pass_queue_index(dep_node);
            uint32_t dep_local_idx = pass_local_to_queue_indices_[dep_node];

            RDGPassNodeRef& closest = closest_nodes_per_queue[dep_queue];
            if (!closest || dep_local_idx > pass_local_to_queue_indices_[closest])
            {
                // 保留同一队列上最晚（最近）执行的依赖
                closest = dep_node;
            }
        }

        // 更新SSIS值和nodes_to_sync_with
        nodes_to_sync.clear();
        for (uint32_t q = 0; q < total_queue_count_; ++q)
        {
            if (RDGPassNodeRef closest = closest_nodes_per_queue[q])
            {
                // 更新SSIS
                if (q != pass_queue)
                {
                    ssis[q] = pass_local_to_queue_indices_[closest];
                }
                // 只保留最近的节点
                nodes_to_sync.push_back(closest);
            }
        }

        // 设置自己队列的SSIS值
        ssis[pass_queue] = pass_local_to_queue_indices_[pass];

        //ENGINE_LOG_INFO("  Pass {} initial SSIS: [{},{},{}], nodes_to_sync: {}",
        //    pass->Name(),
        //    ssis[0] == InvalidSyncIndex ? -1 : (int32_t)ssis[0],
        //    ssis.size() > 1 ? (ssis[1] == InvalidSyncIndex ? -1 : (int32_t)ssis[1]) : -1,
        //    ssis.size() > 2 ? (ssis[2] == InvalidSyncIndex ? -1 : (int32_t)ssis[2]) : -1,
        //    static_cast<uint32_t>(nodes_to_sync.size()));
    }

    // Step 3: 第二阶段 - 通过SSIS比较剔除冗余同步
    for (auto* pass : get_passes(graph))
    {
        uint32_t pass_queue = get_pass_queue_index(pass);
        std::vector<uint32_t>& ssis = pass_ssis_[pass];
        std::vector<RDGPassNodeRef>& nodes_to_sync = pass_nodes_to_sync_with_[pass];

        if (nodes_to_sync.empty())
            continue;

        // 构建需要同步的队列集合
        std::unordered_set<uint32_t> queues_to_sync_with;
        for (RDGPassNodeRef node : nodes_to_sync)
        {
            uint32_t node_queue = get_pass_queue_index(node);
            queues_to_sync_with.insert(node_queue);
        }

        // 优化后的节点列表
        std::vector<RDGPassNodeRef> optimal_nodes_to_sync;

        // 迭代直到所有队列都被覆盖
        while (!queues_to_sync_with.empty())
        {
            std::vector<SyncCoverage> sync_coverage_array;
            uint32_t max_syncs_covered = 0;

            // 计算每个依赖节点的覆盖情况
            for (uint32_t dep_idx = 0; dep_idx < nodes_to_sync.size(); ++dep_idx)
            {
                RDGPassNodeRef dep_node = nodes_to_sync[dep_idx];
                const std::vector<uint32_t>& dep_ssis = pass_ssis_[dep_node];

                // 一个converage 对应一个依赖节点
                SyncCoverage coverage;
                coverage.node_to_sync_with = dep_node;
                coverage.node_index = dep_idx;

                // 检查这个节点能覆盖哪些队列的同步
                for (uint32_t queue_idx : queues_to_sync_with)
                {
                    uint32_t current_desired_sync = ssis[queue_idx];
                    uint32_t dep_sync_index = dep_ssis[queue_idx];

                    // 特殊处理：如果是当前Pass所在的队列，减1
                    // 自己队列上，需要的是前一个位置
                    if (queue_idx == pass_queue && current_desired_sync != InvalidSyncIndex)
                    {
                        current_desired_sync = (current_desired_sync > 0) ? current_desired_sync - 1 : 0;
                    }

                    // 关键判断：dep_node 的 SSIS[q] >= pass 需要的 SSIS[q]
                    // → 同步 dep_node 就隐含同步了 pass 对队列 q 的需求
                    if (dep_sync_index != InvalidSyncIndex && dep_sync_index >= current_desired_sync)
                    {
                        coverage.synced_queue_indices.push_back(queue_idx);
                    }
                }

                if (!coverage.synced_queue_indices.empty())
                {
                    sync_coverage_array.push_back(coverage);
                    max_syncs_covered = std::max(max_syncs_covered,static_cast<uint32_t>(coverage.synced_queue_indices.size()));
                }
            }

            // 找到覆盖最多队列的节点
            bool found_coverage = false;
            uint32_t selected_node_index = InvalidSyncIndex;
            for (const auto& coverage : sync_coverage_array)
            {
                if (coverage.synced_queue_indices.size() == max_syncs_covered)
                {
                    // 只添加跨队列的同步（同队列是隐式的）
                    if (get_pass_queue_index(coverage.node_to_sync_with) != pass_queue)
                    {
                        optimal_nodes_to_sync.push_back(coverage.node_to_sync_with);
                    }

                    // 更新SSIS值（可能比期望的更大）
                    const std::vector<uint32_t>& node_ssis = pass_ssis_[coverage.node_to_sync_with];
                    for (uint32_t q : coverage.synced_queue_indices)
                    {
                        if (node_ssis[q] != InvalidSyncIndex)
                        {
                            ssis[q] = std::max(ssis[q], node_ssis[q]);
                        }
                    }

                    // 移除已覆盖的队列
                    for (uint32_t q : coverage.synced_queue_indices)
                    {
                        queues_to_sync_with.erase(q);
                    }

                    selected_node_index = coverage.node_index;
                    found_coverage = true;
                    break; // 每次迭代只选择一个节点
                }
            }

            if (!found_coverage)
            {
                // 无法覆盖剩余队列，退出
                ENGINE_LOG_INFO("    Warning: Pass {} cannot cover remaining queues", pass->Name());
                break;
            }

            //// 从nodes_to_sync中移除已选择的节点（反向迭代避免索引失效）
            //for (auto it = sync_coverage_array.rbegin(); it != sync_coverage_array.rend(); ++it)
            //{
            //    if (it->synced_queue_indices.size() == max_syncs_covered)
            //    {
            //        nodes_to_sync.erase(nodes_to_sync.begin() + it->node_index);
            //        break;
            //    }
            //}

            nodes_to_sync.erase(nodes_to_sync.begin() + selected_node_index);
        }   

        // 更新优化后的同步点
        for (RDGPassNodeRef optimal_node : optimal_nodes_to_sync)
        {
            // 在原始同步点中找到对应的同步点
            for (const auto& raw_sync : ssis_result_.raw_sync_points)
            {
                if (raw_sync.producer_pass == optimal_node && raw_sync.consumer_pass == pass)
                {
                    ssis_result_.optimized_sync_points.push_back(raw_sync);
                    //ENGINE_LOG_INFO("    Optimized sync: {} -> {}",
                    //    optimal_node->Name(), pass->Name());
                    break;
                }
            }
        }
    }

    //ENGINE_LOG_INFO("CrossQueueSyncAnalysis: SSIS optimization completed - {} sync points after optimization",
    //    static_cast<uint32_t>(ssis_result_.optimized_sync_points.size()));
}

void CrossQueueSyncAnalysis::calculate_optimization_statistics() 
{
    ssis_result_.total_optimized_syncs = static_cast<uint32_t>(ssis_result_.optimized_sync_points.size());
    ssis_result_.sync_reduction_count = ssis_result_.total_raw_syncs - ssis_result_.total_optimized_syncs;

    if (ssis_result_.total_raw_syncs > 0)
    {
        ssis_result_.optimization_ratio = static_cast<float>(ssis_result_.sync_reduction_count) / static_cast<float>(ssis_result_.total_raw_syncs);
    }
    else
    {
        ssis_result_.optimization_ratio = 0.0f;
    }
}

uint32_t CrossQueueSyncAnalysis::get_pass_queue_index(RDGPassNodeRef pass) const 
{
    return queue_schedule_.get_schedule_result().pass_queue_assignments.find(pass)->second;
}

void CrossQueueSyncAnalysis::dump_ssis_analysis() const 
{
    ENGINE_LOG_INFO("========== SSIS Analysis Results ==========");
    ENGINE_LOG_INFO("Raw sync points: {}", ssis_result_.total_raw_syncs);
    ENGINE_LOG_INFO("Optimized sync points: {}", ssis_result_.total_optimized_syncs);
    ENGINE_LOG_INFO("Sync reduction: {} ({})",
        ssis_result_.sync_reduction_count,
        ssis_result_.optimization_ratio * 100.0f);

    ENGINE_LOG_INFO("\nOptimized Sync Points:");
    for (size_t i = 0; i < ssis_result_.optimized_sync_points.size(); ++i)
    {
        const auto& sync = ssis_result_.optimized_sync_points[i];
        const char* type_str = (sync.type == ESyncPointType::Signal) ? "Signal" : "Wait";

        ENGINE_LOG_INFO("  [{}] {}: {} (queue {}) -> {} (queue {}) | Resource: {} | States: {}->{}",
            i, type_str,
            sync.producer_pass->Name(), sync.producer_queue_index,
            sync.consumer_pass->Name(), sync.consumer_queue_index,
            sync.resource->Name(),
            static_cast<int>(sync.from_state),
            static_cast<int>(sync.to_state));
    }

    ENGINE_LOG_INFO("==========================================");
}

void CrossQueueSyncAnalysis::dump_sync_points() const 
{
    dump_ssis_analysis(); // 目前复用相同的输出
}
