#pragma once
#include "pass_info_analysis.h"

struct CrossQueueSyncPoint;
class QueueSchedule;

// 单一资源依赖信息
struct ResourceDependency {
    RDGPassNodeRef dependent_pass;                // The depended pass
    RDGResourceNodeRef resource;                  // The involved resource
    EResourceAccessType current_access;      // Current pass access type
    EResourceAccessType previous_access;     // Previous pass access type
    RHIResourceState current_state;        // Current pass expected resource state
    RHIResourceState previous_state;       // Previous pass resource state
};

// 逻辑拓扑分析结果
struct LogicalTopologyResult
{
    struct DependencyLevel
    {
        uint32_t level = 0;
        std::vector<RDGPassNodeRef> passes;  // 该级别中的所有Pass

        // 统计信息
        uint32_t total_resources_accessed = 0;  // 该级别访问的资源总数
        uint32_t cross_level_dependencies = 0;  // 跨级别依赖数
    };

    // === 逻辑拓扑信息（基于依赖关系，永不变） ===
    std::vector<DependencyLevel> logical_levels;         // 按逻辑依赖级别分组的Pass
    std::vector<RDGPassNodeRef> logical_topological_order;    // 逻辑拓扑排序后的Pass列表
    std::vector<RDGPassNodeRef> logical_critical_path;        // 逻辑关键路径（基于依赖关系）

    // 统计信息
    uint32_t max_logical_dependency_depth = 0;           // 最大逻辑依赖深度
};

// pass逻辑依赖信息
struct PassDependencies {
    // === 逻辑依赖信息（永不变，不受重排序影响） ===
    std::vector<ResourceDependency> resource_dependencies; // All resource dependencies of this pass
    std::vector<RDGPassNodeRef> dependent_passes;    // Pass-level dependencies (extracted from resource dependencies)
    std::vector<RDGPassNodeRef> dependent_by_passes; // Pass-level dependents

    // === 逻辑拓扑信息（基于依赖关系，一次计算永不变） ===
    uint32_t logical_dependency_level = 0;       // 逻辑依赖级别（最长路径深度）
    uint32_t logical_topological_order = 0;      // 逻辑拓扑排序索引
    uint32_t logical_critical_path_length = 0;   // 逻辑关键路径长度（依赖链深度）

    // Query interface
    bool has_dependency_on(RDGPassNodeRef pass) const;
};

// HEFT所有权链序边（QueueSchedule调度后回写：资源所有权链的相邻跨族访问者对——R-R/R-W/W-R。
// 承载QFO所有权链的执行序与SSIS同步点；由RDGCompiler在Phase 3后协调写入并重算拓扑）
struct SchedulingOrderEdge
{
    RDGPassNodeRef from = nullptr;          // 链上前一访问者
    RDGPassNodeRef to = nullptr;            // 链上后一访问者（依赖from）
    RDGResourceNodeRef resource = nullptr;
    EResourceAccessType fromAccess = EResourceAccessType::Read;
    EResourceAccessType toAccess = EResourceAccessType::Read;
};

// Forward declaration
class PassInfoAnalysis;

// Pass dependency analysis phase - 逻辑依赖分析+拓扑排序
class PassDependencyAnalysis : public IRenderGraphPhase
{
public:
    PassDependencyAnalysis(const PassInfoAnalysis& pass_info_analysis);
    ~PassDependencyAnalysis() override = default;

    void reset_for_frame()override;

    // IRenderGraphPhase 接口
    void on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor) override;

    // Query interface - 供后续Phase使用
    const PassDependencies* get_pass_dependencies(RDGPassNodeRef pass) const;
    bool has_dependencies(RDGPassNodeRef pass) const;

    // For QueueSchedule - get pass-level dependencies directly
    const std::vector<RDGPassNodeRef>& get_dependent_passes(RDGPassNodeRef pass) const;
    const std::vector<RDGPassNodeRef>& get_dependent_by_passes(RDGPassNodeRef pass) const;

    // 逻辑拓扑查询 (NEW)
    uint32_t get_dependency_level(RDGPassNodeRef pass) const;
    uint32_t get_topological_order(RDGPassNodeRef pass) const;
    uint32_t get_logical_critical_path_length(RDGPassNodeRef pass) const;
    const LogicalTopologyResult& get_logical_topology_result() const { return logical_topology_; }
    const std::vector<RDGPassNodeRef>& get_topological_order() const { return logical_topology_.logical_topological_order; }
    const std::vector<RDGPassNodeRef>& get_logical_critical_path() const { return logical_topology_.logical_critical_path; }

    // 逻辑并行性查询
    bool can_execute_in_parallel_logically(RDGPassNodeRef pass1, RDGPassNodeRef pass2) const;

    // 跨队列同步点生成 (NEW)
    void generate_cross_queue_sync_points(const QueueSchedule& queue_schedule, std::vector<CrossQueueSyncPoint>& sync_points) const;

    // ---- HEFT回写接口（RDGCompiler在Phase 3后协调调用）----
    // 追加所有权链序边（幂等：pass对+资源去重；边的state对从pass_info_analysis的实际访问取）
    void add_scheduling_order_edges(const std::vector<SchedulingOrderEdge>& edges);
    // 按HEFT全局调度序重建拓扑（序本身合法——就绪集保证前驱在前）。下游生命期/tracker/SSIS
    // 均假设"拓扑序=执行序"：Kahn重算的同层哈希序与调度序不一致会造成生命期区间错位
    //（间歇竞态），故以调度序为唯一权威序
    void apply_schedule_order(const std::vector<RDGPassNodeRef>& order);

    // Debug output
    void dump_dependencies() const;
    void dump_logical_topology() const;
    void dump_logical_critical_path() const;

private:
    // Analysis result: Pass -> its dependency info
    std::unordered_map<RDGPassNodeRef, PassDependencies> pass_dependencies_;

    // Working data for pass dependencies
    struct LastResourceAccess {
        RDGPassNodeRef last_pass = nullptr;
        EResourceAccessType last_access_type = EResourceAccessType::Read;
        RHIResourceState last_state = RESOURCE_STATE_UNDEFINED;
        // [实验v3] 自最近写者以来的读者登记（{pass, 读者访问时状态}）——WAR保护：
        // 后续写者须等全体读者读完（读者间无序，不能只等最近一个）。写者推进锚时清空
        std::vector<std::pair<RDGPassNodeRef, RHIResourceState>> readers_since_write;
    };

    // Logical topology cache
    std::unordered_map<RDGPassNodeRef, uint32_t> in_degrees_;
    std::vector<RDGPassNodeRef> topo_queue_;
    std::vector<uint32_t> topo_levels_;

    // 逻辑拓扑分析结果 (NEW)
    LogicalTopologyResult logical_topology_;

    // Reference to pass info analysis (to avoid recomputation)
    const PassInfoAnalysis& pass_info_analysis;

    // 依赖分析方法
    void analyze_pass_dependencies(RDGDependencyGraphRef graph);

    // 逻辑拓扑分析方法 (NEW)
    void perform_logical_topological_sort_optimized(); // 优化版本：合并拓扑排序和级别计算
    void identify_logical_critical_path();

};
