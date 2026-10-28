#pragma once
#include "pass_dependency_analysis.h"
#include "Function/Render/RHI/RHIStructs.h"   // RHIRenderQueryRef（HEFT权重数据源）
#include <span>

// 队列信息
struct QueueInfo {
    ERenderGraphQueueType type;                         // 队列的类型
    uint32_t index;                                     // 在扁平数组all_queues中的下标
    RHIQueueRef handle;                                 // 队列的句柄                                     
    uint32_t family_index = RHI_QUEUE_FAMILY_IGNORED;   // 队列所在的族
    bool supports_graphics = false;
    bool supports_compute = false;
    bool supports_copy = false;
    bool supports_present = false;
};

// QueueSchedule的输出结果
struct TimelineScheduleResult
{
    std::span<QueueInfo> all_queues;
    std::vector<std::vector<RDGPassNodeRef>> queue_schedules;    // 各队列的调度信息
    std::unordered_map<RDGPassNodeRef, uint32_t> pass_queue_assignments; // Pass到队列的映射
    // HEFT全局调度序（每步选出的pass——全局全序，尊重依赖）。Phase 8 Step A以它为
    // canonical序（topoIndex基准）——流构造序（调度序的队列投影）与排序键必须一致，
    // 否则批次stable_sort打乱提交序、破坏timeline signal单调性（GPU死锁）。
    // 仅空图/零队列早退时为空——Step A兜底走级别序扁平化
    std::vector<RDGPassNodeRef> schedule_order;
};

// Timeline Phase 配置
struct QueueScheduleConfig
{
    uint32_t enable_graphic_queues = MAX_QUEUE_CNT;            // 启用的图像队列数量
    uint32_t enable_async_compute_queues = MAX_QUEUE_CNT;      // 启用的异步计算队列数量
    uint32_t enable_copy_queues = MAX_QUEUE_CNT;               // 启用的拷贝队列数量
    uint32_t max_sync_points = 64;          // 最大同步点数量
    bool enable_debug_output = false;       // 启用调试输出
    double heft_comm_cost_ms = 0.05;        // 跨队列边通信代价c(u,v)（信号+批粒度保守近似）
    double heft_fallback_duration_ms = 0.1; // 无历史EMA的pass默认权重（含cull pass——伪位置零影响面）
    // 分配滞后（稳定性选择，非正确性必需——跨帧正确性=分槽池+帧栅栏+帧末归巢(created)、
    // 跨帧注册表细链(imported)，分配翻转无正确性后果，详见schedule_with_heft权重快照注释）：
    // EFT差在此阈值内保持上帧队列，抑制无意义的逐帧翻转churn
    double heft_hysteresis_ms = 0.0;      // [已禁用]滞后比较版在第4帧Phase6卡死（未查明），帧间稳定改由权重快照实现（见schedule_with_heft）
};

// Timeline Phase - 负责多队列调度
class QueueSchedule : public IRenderGraphPhase
{
public:
    QueueSchedule(PassDependencyAnalysis& dependency_analysis,
        const PassInfoAnalysis& pass_info_analysis,
        const QueueScheduleConfig& config = {},
        RHIRenderQueryRef timingQuery = nullptr);
    ~QueueSchedule() override;

    void reset_for_frame()override;

    // IRenderGraphPhase 接口
    void on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)  override;

    // 静态：按配置枚举 on_execute 将注册的队列集合（顺序 graphics→computes→copies、同开关、同判空），供 RenderSystem 在录制开始前预建每队列的命令池，
    // 保证帧槽 queueSlots 下标与 all_queues 对齐
    static std::vector<RHIQueueRef> QueryConfiguredQueues(const QueueScheduleConfig& config);

    // 获取调度结果
    const TimelineScheduleResult& get_schedule_result() const { return schedule_result; }

    // HEFT所有权链序边（on_execute末尾回写进Phase2并按调度序重建拓扑——见on_execute尾部）
    const std::vector<SchedulingOrderEdge>& get_ownership_order_edges() const { return ownership_edges_; }

    // pass→队列 / 队列→族 查询（数据源=schedule_result/all_queues；原先由CrossQueueSyncAnalysis转发，已收归本类）
    uint32_t get_pass_queue_index(RDGPassNodeRef pass) const;
    uint32_t get_queue_family_index(uint32_t queue_index) const;

    // 调试接口
    void dump_timeline_result(const char* title, const TimelineScheduleResult& R) const ;

private:
    // 初始化阶段
    void query_queue_capabilities(RDGDependencyGraphRef graph) ;

    // 编译阶段
    void assign_passes_to_queues(RDGDependencyGraphRef graph) ;

    // HEFT调度：ranku优先+EFT决策+资源所有权表
    void schedule_with_heft(RDGDependencyGraphRef graph) ;

    // pass在指定队列上是否合格（能力约束：NodeType+flags规则，HEFT的合格队列集）
    bool is_queue_eligible(RDGPassNodeRef pass, uint32_t queueIndex) ;

private:
    QueueScheduleConfig config;

    std::vector<QueueInfo> all_queues;  // 统一管理所有队列，按类型分组

    // 引用传入的依赖分析器（非const——on_execute末尾回写所有权边+按调度序重建拓扑）
    PassDependencyAnalysis& dependency_analysis;

    // 引用传入的pass信息（HEFT的资源访问全集与所有权链构建）
    const PassInfoAnalysis& pass_info_analysis;

    // pass历史耗时EMA（可为空——fallback权重；跨帧槽共享，RenderSystem拥有）
    RHIRenderQueryRef timingQuery;

    // 调度结果输出
    TimelineScheduleResult schedule_result;

    // HEFT所有权链回写边（每帧on_execute重建）
    std::vector<SchedulingOrderEdge> ownership_edges_;
};

// 辅助函数
inline const char* get_queue_type_name(ERenderGraphQueueType type)
{
    switch (type) {
    case ERenderGraphQueueType::Graphics: return "Graphics";
    case ERenderGraphQueueType::AsyncCompute: return "AsyncCompute";
    case ERenderGraphQueueType::Copy: return "Copy";
    default: return "Unknown";
    }
}
