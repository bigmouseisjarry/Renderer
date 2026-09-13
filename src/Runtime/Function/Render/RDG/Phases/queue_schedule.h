#pragma once
#include "pass_dependency_analysis.h"
#include "Function/Render/RHI/RHIStructs.h"   // RHIRenderQueryRef（HEFT权重数据源）
#include <span>

// 队列能力描述
struct QueueCapabilities
{
    bool supports_graphics = false;
    bool supports_compute = false;
    bool supports_copy = false;
    bool supports_present = false;
    uint32_t family_index = 0;
    RHIQueueRef queue_handle = nullptr;  // 队列句柄
};

struct QueueInfo {
    ERenderGraphQueueType type;
    uint32_t index;
    RHIQueueRef handle;
    uint32_t family_index = RHI_QUEUE_FAMILY_IGNORED;    // 队列族（所有权转移release/acquire判定用）
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
    // HEFT全局调度序（每步选出的pass——全局全序，尊重依赖）。非空时Phase 8 Step A以它为
    // canonical序（topoIndex基准）替代级别序扁平化——流构造序（调度序的队列投影）与排序键
    // 必须一致，否则批次stable_sort打乱提交序、破坏timeline signal单调性（GPU死锁）。
    // 旧classify路径恒空=Step A走级别序
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
    // HEFT调度（2026-09-11）：ranku优先级+EFT决策+资源所有权表（EXCLUSIVE纹理跨族访问的互斥
    // 排队），所有权链相邻异族访问者对回写为依赖边（SchedulingOrderEdge）供Phase2重算拓扑——
    // 取代classify按NodeType+ForceGraphicsQueue白名单+同层注册序的三重偶然分派。
    // false=旧classify路径（A/B对照用；旧路径下所有权链不生成，QFO执行序由Phase2的WAR/R-R静态边兜底）
    bool use_heft = true;
    double heft_comm_cost_ms = 0.05;        // 跨队列边通信代价c(u,v)（信号+批粒度保守近似）
    double heft_fallback_duration_ms = 0.1; // 无历史EMA的pass默认权重（含cull pass——伪位置零影响面）
    // 分配滞后（正确性必需，非优化）：池化(created)纹理跨帧复用同一物理内存且细链只回看一帧
    // ——跨帧跨队列WAR靠"队列分配帧间稳定"隐式兜底（旧classify天然满足）。EMA波动使EFT
    // 帧间翻转即触发该竞态（画面闪烁，三裁判帧内视角均不可见）。EFT差在此阈值内保持上帧队列
    double heft_hysteresis_ms = 0.0;      // [已禁用]滞后比较版在第4帧Phase6卡死（未查明），稳定性改由权重快照实现（见schedule_with_heft）
    // [二分诊断]锁合格队列=旧classify语义（compute无flag仅AsyncCompute，copy钉graphics），
    // 保留HEFT的调度序/所有权表/回写边——等分配A/B对照用（2026-09-12闪烁定案：分配无关，
    // 罪在隐形依赖边，已补边修复）
    bool heft_lock_classify_assignment = false;
};

// Timeline Phase - 负责多队列调度
class QueueSchedule : public IRenderGraphPhase
{
public:
    QueueSchedule(const PassDependencyAnalysis& dependency_analysis,
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

    // HEFT所有权链序边（调度后回写源）——RDGCompiler协调写入Phase2并重算拓扑
    const std::vector<SchedulingOrderEdge>& get_ownership_order_edges() const { return ownership_edges_; }

    // 调试接口
    void dump_timeline_result(const char* title, const TimelineScheduleResult& R) const ;

private:
    // 初始化阶段
    void query_queue_capabilities(RDGDependencyGraphRef graph) ;

    // 编译阶段
    void assign_passes_to_queues(RDGDependencyGraphRef graph) ;
    void assign_passes_using_topology() ;

    // HEFT调度（use_heft=true）：ranku优先+EFT决策+资源所有权表，输出与旧路径同一结构
    void schedule_with_heft(RDGDependencyGraphRef graph) ;

    // Pass分类和调度
    ERenderGraphQueueType classify_pass(RDGPassNodeRef pass) ;

    // pass在指定队列上是否合格（能力约束：NodeType+flags规则，HEFT的合格队列集）
    bool is_queue_eligible(RDGPassNodeRef pass, uint32_t queueIndex) ;

private:
    QueueScheduleConfig config;

    std::vector<QueueInfo> all_queues;  // 统一管理所有队列，按类型分组

    // 引用传入的依赖分析器
    const PassDependencyAnalysis& dependency_analysis;

    // 引用传入的pass信息（HEFT的资源访问全集与所有权链构建）
    const PassInfoAnalysis& pass_info_analysis;

    // pass历史耗时EMA（可为空——fallback权重；跨帧槽共享，RenderSystem拥有）
    RHIRenderQueryRef timingQuery;

    // 调度结果输出
    TimelineScheduleResult schedule_result;

    // HEFT所有权链回写边（每帧on_execute重建）
    std::vector<SchedulingOrderEdge> ownership_edges_;

    uint32_t find_graphics_queue() const ;
    uint32_t find_least_loaded_compute_queue() const ;
    uint32_t find_copy_queue() const ;
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
