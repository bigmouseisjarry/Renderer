#include "queue_schedule.h"
#include "pass_dependency_analysis.h"

#include "Function/Global/EngineContext.h"


QueueSchedule::QueueSchedule(const PassDependencyAnalysis& dependency_analysis,
    const QueueScheduleConfig& cfg)
    : config(cfg), dependency_analysis(dependency_analysis)
{}

QueueSchedule::~QueueSchedule() = default;

void QueueSchedule::reset_for_frame()
{
    all_queues.clear();
}

void QueueSchedule::on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor) 
{
    //ENGINE_TIME_SCOPE("QueueSchedule");
    //ENGINE_LOG_INFO("QueueSchedule: Starting timeline scheduling and fence allocation");

    // 查询队列
    query_queue_capabilities(graph);

    // 分配 Pass
    assign_passes_to_queues(graph);

    // 可选：输出调试信息
    if (config.enable_debug_output) {
        dump_timeline_result("Timeline Schedule", schedule_result);
    }
}

void QueueSchedule::query_queue_capabilities(RDGDependencyGraphRef graph)
{
    // 清空队列信息（简化为单一数组）
    uint32_t queue_index = 0;
    auto Backend = EngineContext::RHI();

    // 添加Graphics队列（总是存在）
    if (auto gfx_queue = Backend->GetQueue({ QUEUE_TYPE_GRAPHICS, 0 })) {
        all_queues.push_back(QueueInfo{
            .type = ERenderGraphQueueType::Graphics,
            .index = queue_index++,
            .handle = gfx_queue,
            .supports_graphics = true,
            .supports_compute = true,
            .supports_copy = true,
            .supports_present = true
            });
    }

    // 添加AsyncCompute队列（多个）
    if (config.enable_async_compute) {
        for (uint32_t i = 0; i < config.max_async_compute_queues; ++i) {
            if (auto cmpt_queue = Backend->GetQueue({ QUEUE_TYPE_COMPUTE, i })) {
                all_queues.push_back(QueueInfo{
                    .type = ERenderGraphQueueType::AsyncCompute,
                    .index = queue_index++,
                    .handle = cmpt_queue,
                    .supports_compute = true,
                    .supports_copy = true
                    });
            }
        }
    }

    // 添加Copy队列（多个）
    if (config.enable_copy_queue) {
        for (uint32_t i = 0; i < config.max_copy_queues; ++i) {
            if (auto cpy_queue = Backend->GetQueue({ QUEUE_TYPE_TRANSFER, i })) {
                all_queues.push_back(QueueInfo{
                    .type = ERenderGraphQueueType::Copy,
                    .index = queue_index++,
                    .handle = cpy_queue,
                    .supports_copy = true
                    });
            }
        }
    }

    //// 简化的调试输出
    //uint32_t graphics_count = 0, compute_count = 0, copy_count = 0;
    //for (const auto& queue : all_queues) {
    //    switch (queue.type) {
    //    case ERenderGraphQueueType::Graphics: graphics_count++; break;
    //    case ERenderGraphQueueType::AsyncCompute: compute_count++; break;
    //    case ERenderGraphQueueType::Copy: copy_count++; break;
    //    default: break;
    //    }
    //}

    //ENGINE_LOG_INFO("QueueSchedule: Queue setup - Graphics: {}, AsyncCompute: {}, Copy: {}",
    //    graphics_count, compute_count, copy_count);
}

// 给定一个 Pass，决定它希望在哪种类型的队列上执行。
ERenderGraphQueueType QueueSchedule::classify_pass(RDGPassNodeRef pass)
{
    // 1. Present Pass必须在Graphics队列
    if (pass->NodeType() == RDGPassNodeType::RDG_PASS_NODE_TYPE_PRESENT) {
        return ERenderGraphQueueType::Graphics;
    }

    // 2. Render Pass必须在Graphics队列
    if (pass->NodeType() == RDGPassNodeType::RDG_PASS_NODE_TYPE_RENDER) {
        return ERenderGraphQueueType::Graphics;
    }

    // 3. Copy Pass优先Copy队列（如果标记为可独立执行）
    if (pass->NodeType() == RDGPassNodeType::RDG_PASS_NODE_TYPE_COPY && config.enable_copy_queue) {
        //auto* copy_pass = static_cast<RDGCopyPassNodeRef>(pass);
        //if (copy_pass->get_can_be_lone()) {
        //    return ERenderGraphQueueType::Copy;
        //}
        auto* copy_pass = static_cast<RDGCopyPassNodeRef>(pass);
        if (!copy_pass->IsGenerateMip()) {        // 非 mipmap 拷贝才能去 Copy 队列
            return ERenderGraphQueueType::Copy;
        }
    }

    // 4. Compute Pass仅基于手动标记
    if (pass->NodeType() == RDGPassNodeType::RDG_PASS_NODE_TYPE_COMPUTE && config.enable_async_compute)
    {
        if (!pass->has_flags(RDGPassFlags::ForceGraphicsQueue)) {
            return ERenderGraphQueueType::AsyncCompute;
        }
    }

    // 默认分配到Graphics队列
    return ERenderGraphQueueType::Graphics;
}

void QueueSchedule::assign_passes_to_queues(RDGDependencyGraphRef graph)
{
    // 拷贝队列信息到结果
    schedule_result.all_queues = all_queues;
    // 每个队列一个 pass 列表
    schedule_result.queue_schedules.resize(all_queues.size());

    // 拓扑驱动的分配
    assign_passes_using_topology();
}

// 利用 PassDependencyAnalysis 的依赖级别信息，按级别顺序遍历所有 Pass，将每个 Pass 分配到合适的队列
void QueueSchedule::assign_passes_using_topology()
{
    // 获取逻辑拓扑排序结果
    const auto& topology_result = dependency_analysis.get_logical_topology_result();

    //ENGINE_LOG_INFO("QueueSchedule: Using topology-based scheduling with {} dependency levels",
    //    topology_result.max_logical_dependency_depth + 1);

    // 按依赖级别顺序调度Pass，确保依赖正确性
    for (const auto& level : topology_result.logical_levels)
    {
        //ENGINE_LOG_INFO("  Processing dependency level {} with {} passes",
        //    level.level, static_cast<uint32_t>(level.passes.size()));

        // 在同一依赖级别内，按照拓扑顺序分配Pass到队列
        for (auto* pass : level.passes)
        {
            // 分类Pass并找到合适的队列
            ERenderGraphQueueType preferred_queue_type = classify_pass(pass);
            uint32_t target_queue_index;

            // 根据队列类型找到实际的队列索引
            switch (preferred_queue_type)
            {
            case ERenderGraphQueueType::Graphics:
                target_queue_index = find_graphics_queue();
                break;
            case ERenderGraphQueueType::AsyncCompute:
                target_queue_index = find_least_loaded_compute_queue();
                break;
            case ERenderGraphQueueType::Copy:
                target_queue_index = find_copy_queue();
                break;
            default:
                target_queue_index = find_graphics_queue();
                break;
            }

            // 将Pass添加到选定的队列
            if (target_queue_index < schedule_result.queue_schedules.size())
            {
                schedule_result.queue_schedules[target_queue_index].push_back(pass);
                schedule_result.pass_queue_assignments[pass] = target_queue_index;

                //ENGINE_LOG_INFO("    Assigned pass '{}' to {} queue (index {})",
                //    pass->Name(),
                //    get_queue_type_name(all_queues[target_queue_index].type),
                //    target_queue_index);
            }
            else
            {
                //SPDLOG_ERROR("QueueSchedule: Invalid queue index {} for pass '{}'",
                //    target_queue_index, pass->Name());
            }
        }
    }

    //ENGINE_LOG_INFO("QueueSchedule: Topology-based scheduling completed");
}

uint32_t QueueSchedule::find_graphics_queue() const
{
    for (uint32_t i = 0; i < all_queues.size(); ++i) {
        if (all_queues[i].type == ERenderGraphQueueType::Graphics) {
            return i;
        }
    }
    return 0; // 应该总是有Graphics队列
}

uint32_t QueueSchedule::find_least_loaded_compute_queue() const
{
    // 简化轮询：找到第一个计算队列即可，避免复杂的负载计算
    static uint32_t next_compute_index = 0;

    std::vector<uint32_t> compute_queues;
    for (uint32_t i = 0; i < all_queues.size(); ++i) {
        if (all_queues[i].type == ERenderGraphQueueType::AsyncCompute) {
            compute_queues.push_back(i);
        }
    }

    if (compute_queues.empty()) {
        return find_graphics_queue(); // 回退
    }

    uint32_t queue_idx = compute_queues[next_compute_index % compute_queues.size()];
    next_compute_index++;
    return queue_idx;
}

uint32_t QueueSchedule::find_copy_queue() const
{
    for (uint32_t i = 0; i < all_queues.size(); ++i) {
        if (all_queues[i].type == ERenderGraphQueueType::Copy) {
            return i;
        }
    }
    return find_graphics_queue(); // 回退
}

void QueueSchedule::dump_timeline_result(const char* title, const TimelineScheduleResult& R) const
{
    ENGINE_LOG_INFO("═══════════════════════════════════════");
    ENGINE_LOG_INFO("{}", title);
    ENGINE_LOG_INFO("═══════════════════════════════════════");

    // 打印队列调度信息
    ENGINE_LOG_INFO(" Queue Schedules ({} queues):", (int)R.queue_schedules.size());
    for (size_t i = 0; i < R.queue_schedules.size(); ++i) {
        const auto& queue_schedule = R.queue_schedules[i];
        const auto& queue_info = R.all_queues[i];
        const char* queue_name = get_queue_type_name(queue_info.type);

        // 为多队列类型添加索引标识
        std::string queue_display_name;
        if (queue_info.type == ERenderGraphQueueType::AsyncCompute && all_queues.size() > 1) {
            uint32_t async_idx = 0;
            for (size_t k = 0; k < i; ++k) {
                if (all_queues[k].type == ERenderGraphQueueType::AsyncCompute)
                    async_idx++;
            }
            queue_display_name = std::format("{}#{}", queue_name, async_idx);
        }
        else {
            queue_display_name = std::string(queue_name);
        }

        ENGINE_LOG_INFO("  [{}] {} Queue (index={}, {} passes):",
            i, queue_display_name.c_str(), queue_info.index, (int)queue_schedule.size());

        for (size_t j = 0; j < queue_schedule.size(); ++j) {
            auto* pass = queue_schedule[j];
            const char* pass_type_name = "Unknown";

            switch (pass->NodeType()) {
            case RDGPassNodeType::RDG_PASS_NODE_TYPE_RENDER: pass_type_name = "Render"; break;
            case RDGPassNodeType::RDG_PASS_NODE_TYPE_COMPUTE: pass_type_name = "Compute"; break;
            case RDGPassNodeType::RDG_PASS_NODE_TYPE_COPY: pass_type_name = "Copy"; break;
            case RDGPassNodeType::RDG_PASS_NODE_TYPE_PRESENT: pass_type_name = "Present"; break;
            default: break;
            }

            ENGINE_LOG_INFO("    [{}] {} Pass (name={})", j, pass_type_name, pass->Name());
        }
    }

    // 打印Pass映射统计
    ENGINE_LOG_INFO("");
    ENGINE_LOG_INFO("Pass Assignment Summary:");

    uint32_t graphics_count = 0, compute_count = 0, copy_count = 0;
    for (const auto& [pass, queue_idx] : R.pass_queue_assignments)
    {
        if (queue_idx < R.queue_schedules.size()) {
            auto queue_type = R.all_queues[queue_idx].type;
            switch (queue_type) {
            case ERenderGraphQueueType::Graphics: graphics_count++; break;
            case ERenderGraphQueueType::AsyncCompute: compute_count++; break;
            case ERenderGraphQueueType::Copy: copy_count++; break;
            default: break;
            }
        }
    }

    ENGINE_LOG_INFO(" Graphics Queue: {} passes", graphics_count);
    ENGINE_LOG_INFO(" AsyncCompute Queue: {} passes", compute_count);
    ENGINE_LOG_INFO(" Copy Queue: {} passes", copy_count);
    ENGINE_LOG_INFO(" Total Passes: {}", (int)R.pass_queue_assignments.size());
}