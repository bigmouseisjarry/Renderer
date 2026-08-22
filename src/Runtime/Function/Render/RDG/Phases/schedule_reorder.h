#pragma once
#include "pass_info_analysis.h"
#include "pass_dependency_analysis.h"
#include "queue_schedule.h"

struct ExecutionReorderConfig {
    // 缓存局部性优化
    // 如果 Pass A 和 Pass B 访问相同的资源（纹理、buffer），把 A 和 B 在时间线上拉近, 提高GPU cache 命中率
    bool enable_cache_opt = true;     

    // 资源生命周期压缩
    // 如果一个资源的 producer 和 consumer 离得很远，中间一大堆不相关 Pass占着内存，把 consumer 往前"吸引"→ 资源可以更早释放，降低峰值显存
    bool enable_lifetime_opt = true;   

    // 最大吸引距离
    uint32_t max_attraction_distance = 10;  

    // 最小亲和度阈值
    // 两个 Pass 的"资源重叠度"必须 ≥ 0.1 才值得吸引。
    // affinity_score = 共享资源数 / max(A的资源数, B的资源数)
    float min_affinity_score = 0.1f;        
};

// 重排序结果
struct ExecutionReorderResult {
    // 外层：每个队列一个条目（与 QueueSchedule 的队列数一一对应）
    // 内层：该队列上 Pass 的时间线顺序（重排后）
    // optimized_timeline 是 QueueSchedule::queue_schedules 的重排副本
    std::vector<std::vector<RDGPassNodeRef>> optimized_timeline;
};

// Simplified - use RenderGraph DAG directly instead of rebuilding resource chains
// 不对，应该是基于 PassDependencyAnalysis中的LogicalTopologyResult，更具每一个level在每一条queue上对pass做局部重排序
class ExecutionReorderPhase : public IRenderGraphPhase 
{
public:
    ExecutionReorderPhase(
        const PassInfoAnalysis& pass_info,
        const PassDependencyAnalysis& dependency_analysis,
        const QueueSchedule& timeline,
        const ExecutionReorderConfig& config = {});
    ~ExecutionReorderPhase() override = default;

    void reset_for_frame()override;
    void on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)  override;

    // 结果访问
    const ExecutionReorderResult& get_result() const { return result; }
    const std::vector<std::vector<RDGPassNodeRef>>& get_optimized_timeline() const { return result.optimized_timeline; }

private:
    // 顶层入口，遍历所有队列做优化
    void run_graph_based_optimization() ;

    // 对单个队列的时间线做重排，逐 Pass 尝试吸引
    void optimize_queue_with_graph(uint32_t queue_idx) ;
    // 安全性检查：把 Pass 从 current_pos 移到 target_pos 是否会违反依赖关系？重排不能破坏 DAG 拓扑序
    bool can_attract_pass_safely(uint32_t queue_idx, size_t current_pos, size_t target_pos) ;
    // 执行实际的移动——从时间线 from_pos 摘除，插入到 to_pos
    void attract_pass(uint32_t queue_idx, size_t from_pos, size_t to_pos) ;

    // 吸引力判定：两个 Pass 是否值得拉近？统一了缓存优化和生命周期压缩两种判据，避免重复扫描资源
    bool should_attract_passes(RDGPassNodeRef current_pass, RDGPassNodeRef target_pass, bool check_cache, bool check_lifetime) const ;

    // DAG 路径查询：from 到 to 之间是否存在依赖路径？用于安全性检查
    bool has_path_between_passes(RDGPassNodeRef from_pass, RDGPassNodeRef to_pass) const ;
    // 查询两个 Pass 共享的资源集合
    std::vector<RDGResourceNodeRef> get_shared_resources(RDGPassNodeRef pass1, RDGPassNodeRef pass2) const ;

    // 已知共享资源集合时的亲和度计算
    float calculate_resource_affinity_from_shared(RDGPassNodeRef pass1, RDGPassNodeRef pass2, const std::vector<RDGResourceNodeRef>& shared_resources) const ;

private:
    // 配置
    ExecutionReorderConfig config;

    const PassInfoAnalysis& pass_info_analysis;
    const PassDependencyAnalysis& dependency_analysis;
    const QueueSchedule& timeline_schedule;

    // 可修改的时间线副本
    std::vector<std::vector<RDGPassNodeRef>> working_timeline;

    // 直接引用 DAG 做路径查询
    RDGDependencyGraphRef render_graph = nullptr;

    // 结果
    ExecutionReorderResult result;
};