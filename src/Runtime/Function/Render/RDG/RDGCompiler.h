#pragma once

#include "Function/Render/RDG/RDGNode.h"
#include "Function/Render/RDG/Phases/IRenderGraphPhase.h"
#include "Function/Render/RDG/Phases/pass_info_analysis.h"
#include "Function/Render/RDG/Phases/pass_dependency_analysis.h"
#include "Function/Render/RDG/Phases/queue_schedule.h"
#include "Function/Render/RDG/Phases/schedule_reorder.h"
#include "Function/Render/RDG/Phases/cross_queue_sync_analysis.h"
#include "Function/Render/RDG/Phases/pass_binding_phase.h"
#include "Function/Render/RDG/Phases/barrier_generation_phase.h"
#include "Function/Render/RDG/Phases/pass_execution_phase.h"


// RDGCompiler：编排 Phase pipeline
//
// 职责：
//   1. 构造 Phase 实例并建立引用链（上游 → 下游）
//   2. 按顺序执行 Phase 的 on_execute
//
// 图的所有权在 RenderSystem/RDGBuilder，Compiler 不持有图
// Phase 之间通过构造时传入的 const 引用传递上游结果
//
class RDGCompiler
{
public:
    RDGCompiler() = default;
    RDGCompiler(const QueueScheduleConfig& queueConfig = {}, const ExecutionReorderConfig& reorderConfig = {}, const CrossQueueSyncConfig& queueSyncConfig = {}
        , const PassBindingConfig& bindingConfig = {}, const BarrierGenerationConfig& barrierConfig = {}, const CommandRecordingConfig& executionConfig = {})
        : queueConfig_(queueConfig),reorderConfig_(reorderConfig), queueSyncConfig_(queueSyncConfig), bindingConfig_(bindingConfig), barrierConfig_(barrierConfig), executionConfig_(executionConfig) {}

    void reset();

    void compile_and_execute(RDGDependencyGraphRef graph, RDGPerFrameResource* executor);

    // 是否执行阶段6-8（绑定/屏障/录制，有真实副作用：池分配与命令录制）。
    // 关闭时退回纯分析模式（阶段1-5，无副作用），由 RenderSystem 走旧路径 rdgBuilder->Execute()——
    // 注意两条路径不能同时开启，否则同一命令流被录制两遍且描述符集泄漏
    //bool enablePhaseExecution = true;

    const PassInfoAnalysis& GetPassInfoAnalysis() const { return passInfoAnalysis; }
    const PassDependencyAnalysis& GetPassDependencyAnalysis() const { return passDependencyAnalysis; }
    const QueueSchedule& GetQueueSchedule() const { return queueSchedule; }
    const ExecutionReorderPhase& GetExecutionReorder() const { return executionReorder; }
    const CrossQueueSyncAnalysis& GetCrossQueueSyncAnalysis() const { return crossQueueSyncAnalysis; }
    const PassBindingPhase& GetPassBindingPhase() const { return passBinding; }
    const BarrierGenerationPhase& GetBarrierGenerationPhase() const { return barrierGeneration; }
    const PassExecutionPhase& GetPassExecutionPhase() const { return passExecution; }

private:
    // 阶段 1: 信息收集
    PassInfoAnalysis passInfoAnalysis;

    // 阶段 2: 依赖分析（依赖阶段1，通过 const 引用传入）
    PassDependencyAnalysis passDependencyAnalysis{ passInfoAnalysis };

    // 阶段3
    QueueScheduleConfig queueConfig_;                          
    QueueSchedule queueSchedule{ passDependencyAnalysis, queueConfig_ };  

    // 阶段4
    ExecutionReorderConfig reorderConfig_;
    ExecutionReorderPhase executionReorder{ passInfoAnalysis,passDependencyAnalysis,queueSchedule,reorderConfig_ };

    // 阶段5
    CrossQueueSyncConfig queueSyncConfig_;
    CrossQueueSyncAnalysis crossQueueSyncAnalysis{ passDependencyAnalysis,queueSchedule,queueSyncConfig_ };

    // 阶段6: 绑定阶段（合并 SakuraEngine 的 resource_allocation + memory_aliasing(Tier0) + bind_table）
    PassBindingConfig bindingConfig_;
    PassBindingPhase passBinding{ passInfoAnalysis,crossQueueSyncAnalysis,bindingConfig_ };

    // 阶段7: 屏障生成
    BarrierGenerationConfig barrierConfig_;
    BarrierGenerationPhase barrierGeneration{ crossQueueSyncAnalysis,passBinding,passInfoAnalysis,executionReorder,barrierConfig_ };

    // 阶段8: Pass执行——实际录制command，替代 RDGBuilder::Execute()
    CommandRecordingConfig executionConfig_;
    PassExecutionPhase passExecution{ queueSchedule,executionReorder,crossQueueSyncAnalysis,barrierGeneration,passBinding,executionConfig_ };
};
using RDGCompilerRef = std::shared_ptr<RDGCompiler>;