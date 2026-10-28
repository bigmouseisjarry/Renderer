#pragma once
#include "pass_info_analysis.h"

class QueueSchedule;

// 单一资源依赖信息
struct ResourceDependency {
    RDGPassNodeRef dependent_pass;                  // 被依赖的pass
    RDGResourceNodeRef resource;                    // 涉及的资源
    EResourceAccessType current_access;             // 当前pass对该资源的访问类型
    EResourceAccessType previous_access;            // 上一个pass对该资源的访问类型
    RHIResourceState current_state;                 // 当前pass期待该资源处于的state
    RHIResourceState previous_state;                // 上一个pass时该资源处于的state
};

// 逻辑拓扑分析结果。注意两个生产点的分工（2026-10-24瘦身定案）：
//   预排序（perform_logical_topological_sort_optimized）只产出logical_topological_order——
//   反正调度后apply_schedule_order会整套覆盖，预排序阶段的级别/关键路径纯属白算，
//   唯一预调度消费者是HEFT ranku的逆拓扑DP；
//   apply_schedule_order（调度后）才是级别分组/关键路径/逐pass索引的权威生产点
struct LogicalTopologyResult
{
    struct DependencyLevel
    {
        uint32_t level = 0;
        std::vector<RDGPassNodeRef> passes;  // 该级别中的所有Pass
    };

    std::vector<RDGPassNodeRef> logical_topological_order;    // 拓扑排序后的Pass列表（调度后=调度序）
    std::vector<DependencyLevel> logical_levels;             // 按逻辑依赖级别分组的Pass（仅apply后有效）
    std::vector<RDGPassNodeRef> logical_critical_path;        // 逻辑关键路径（仅apply后有效）
    uint32_t max_logical_dependency_depth = 0;               // 最大逻辑依赖深度（仅apply后有效）
};

// pass逻辑依赖信息
struct PassDependencies {
    std::vector<ResourceDependency> resource_dependencies;  // 这个pass的所有资源依赖信息
    std::vector<RDGPassNodeRef> dependent_passes;           // 这个pass依赖的所有pass
    std::vector<RDGPassNodeRef> dependent_by_passes;        // 所有依赖这个pass的pass

    uint32_t logical_dependency_level = 0;       // 这个pass的逻辑依赖级别（最长路径深度，仅apply后有效）
    uint32_t logical_topological_order = 0;      // 这个pass的逻辑拓扑排序索引（仅apply后有效）
    uint32_t logical_critical_path_length = 0;   // 这个pass的逻辑关键路径长度（依赖链深度，仅apply后有效）

    // 是否对参数pass存在依赖
    bool has_dependency_on(RDGPassNodeRef pass) const;
};

// HEFT所有权链序边（QueueSchedule调度后回写：资源所有权链的相邻跨族访问者对——R-R/R-W/W-R。
// 承载QFO所有权链的执行序与SSIS同步点；由QueueSchedule.on_execute末尾写入并重算拓扑）
struct SchedulingOrderEdge
{
    RDGPassNodeRef from = nullptr;          // 链上前一访问者
    RDGPassNodeRef to = nullptr;            // 链上后一访问者（依赖from）
    RDGResourceNodeRef resource = nullptr;
    EResourceAccessType fromAccess = EResourceAccessType::Read;
    EResourceAccessType toAccess = EResourceAccessType::Read;
};

class PassInfoAnalysis;

// 逻辑依赖分析+拓扑排序
class PassDependencyAnalysis : public IRenderGraphPhase
{
public:
    PassDependencyAnalysis(const PassInfoAnalysis& pass_info_analysis);
    ~PassDependencyAnalysis() override = default;

    void reset_for_frame()override;

    void on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor) override;

    const PassDependencies* get_pass_dependencies(RDGPassNodeRef pass) const;
    bool has_dependencies(RDGPassNodeRef pass) const;

    // For QueueSchedule - get pass-level dependencies directly
    const std::vector<RDGPassNodeRef>& get_dependent_passes(RDGPassNodeRef pass) const;
    const std::vector<RDGPassNodeRef>& get_dependent_by_passes(RDGPassNodeRef pass) const;

    // 拓扑序查询（唯一消费者=HEFT ranku的逆拓扑DP；调度后=调度序。下游binding/barrier已直取
    // QueueSchedule::schedule_order，不经此处）
    const LogicalTopologyResult& get_logical_topology_result() const { return logical_topology_; }
    uint32_t get_dependency_level(RDGPassNodeRef pass) const;
    uint32_t get_topological_order(RDGPassNodeRef pass) const;
    uint32_t get_logical_critical_path_length(RDGPassNodeRef pass) const;
    const std::vector<RDGPassNodeRef>& get_logical_critical_path() const { return logical_topology_.logical_critical_path; }

    // 逻辑并行性查询
    bool can_execute_in_parallel_logically(RDGPassNodeRef pass1, RDGPassNodeRef pass2) const;

    // 跨队列同步点生成 (NEW)

    // ---- HEFT回写接口（QueueSchedule.on_execute末尾调用——调度完成后立即回写）----
    // 追加所有权链序边（幂等：pass对+资源去重；边的state对从pass_info_analysis的实际访问取）
    void add_scheduling_order_edges(const std::vector<SchedulingOrderEdge>& edges);
    // 按HEFT全局调度序重建拓扑（序本身合法——就绪集保证前驱在前）。下游生命期/tracker/SSIS
    // 均假设"拓扑序=执行序"：Kahn重算的同层哈希序与调度序不一致会造成生命期区间错位
    //（间歇竞态），故以调度序为唯一权威序
    void apply_schedule_order(const std::vector<RDGPassNodeRef>& order);

    // Debug 输出
    void dump_dependencies() const;
    void dump_logical_topology() const;
    void dump_logical_critical_path() const;

private:
    // Pass -> 该pass的依赖信息
    std::unordered_map<RDGPassNodeRef, PassDependencies> pass_dependencies_;

    // 辅助结构体
    struct LastResourceAccess {
        RDGPassNodeRef last_pass = nullptr;                                             // 上一个写该资源的pass
        EResourceAccessType last_access_type = EResourceAccessType::Read;               // 上一个写该资源的pass的访问方式
        RHIResourceState last_state = RESOURCE_STATE_UNDEFINED;                         // 该资源在上一个写该资源的pass时处于的state
        std::vector<std::pair<RDGPassNodeRef, RHIResourceState>> readers_since_write;   // 记录从上一次写者pass到现在中的所有读者pass
    };


    // 逻辑拓扑分析结果
    LogicalTopologyResult logical_topology_;

    const PassInfoAnalysis& pass_info_analysis;

    // 建依赖边
    void analyze_pass_dependencies(RDGDependencyGraphRef graph);

    // Kahn拓扑排序（只产出logical_topological_order——级别/关键路径由apply_schedule_order权威生产）
    void perform_logical_topological_sort_optimized();
    void identify_logical_critical_path();

};
