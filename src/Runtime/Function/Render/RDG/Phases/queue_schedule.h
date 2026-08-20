#pragma once
#include "pass_dependency_analysis.h"
#include <span>

// 队列类型定义
enum class ERenderGraphQueueType : uint8_t
{
    Graphics = 0,      // 主图形队列
    AsyncCompute = 1,  // 异步计算队列
    Copy = 2,          // 专用拷贝队列
    Count = 3
};

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
};

// Timeline Phase 配置
struct QueueScheduleConfig
{
    bool enable_async_compute = true;       // 启用异步计算
    bool enable_copy_queue = true;          // 启用拷贝队列
    uint32_t max_async_compute_queues = MAX_QUEUE_CNT;  // 最大异步计算队列数量
    uint32_t max_copy_queues = MAX_QUEUE_CNT;           // 最大拷贝队列数量
    uint32_t max_sync_points = 64;          // 最大同步点数量
    bool enable_debug_output = false;       // 启用调试输出
};

// Timeline Phase - 负责多队列调度
class QueueSchedule : public IRenderGraphPhase
{
public:
    QueueSchedule(const PassDependencyAnalysis& dependency_analysis,
        const QueueScheduleConfig& config = {});
    ~QueueSchedule() override;

    void reset_for_frame()override;

    // IRenderGraphPhase 接口
    void on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)  override;

    // 获取调度结果
    const TimelineScheduleResult& get_schedule_result() const { return schedule_result; }

    // 调试接口
    void dump_timeline_result(const char* title, const TimelineScheduleResult& R) const ;

private:
    // 初始化阶段
    void query_queue_capabilities(RDGDependencyGraphRef graph) ;

    // 编译阶段
    void assign_passes_to_queues(RDGDependencyGraphRef graph) ;
    void assign_passes_using_topology() ;

    // Pass分类和调度
    ERenderGraphQueueType classify_pass(RDGPassNodeRef pass) ;

private:
    QueueScheduleConfig config;

    std::vector<QueueInfo> all_queues;  // 统一管理所有队列，按类型分组

    // 引用传入的依赖分析器
    const PassDependencyAnalysis& dependency_analysis;

    // 调度结果输出
    TimelineScheduleResult schedule_result;

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
