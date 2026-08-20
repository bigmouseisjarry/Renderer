#pragma once

#include "Function/Render/RDG/RDGNode.h"
#include "Function/Render/RDG/Phases/IRenderGraphPhase.h"
#include "Function/Render/RDG/Phases/pass_info_analysis.h"
#include "Function/Render/RDG/Phases/pass_dependency_analysis.h"
#include "Function/Render/RDG/Phases/queue_schedule.h"

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
    RDGCompiler(const QueueScheduleConfig& queueConfig)
        : queueConfig_(queueConfig) {}

    void reset();

    void compile_and_execute(RDGDependencyGraphRef graph, RDGPerFrameResource* executor);

    const PassInfoAnalysis& GetPassInfoAnalysis() const { return passInfoAnalysis; }
    const PassDependencyAnalysis& GetPassDependencyAnalysis() const { return passDependencyAnalysis; }
    const QueueSchedule& GetQueueSchedule() const { return queueSchedule; }

private:
    // 阶段 1: 信息收集
    PassInfoAnalysis passInfoAnalysis;
    // 阶段 2: 依赖分析（依赖阶段1，通过 const 引用传入）
    PassDependencyAnalysis passDependencyAnalysis{ passInfoAnalysis };

    QueueScheduleConfig queueConfig_;                          // 存储 config
    QueueSchedule queueSchedule{ passDependencyAnalysis, queueConfig_ };  // 阶段3
};
using RDGCompilerRef = std::shared_ptr<RDGCompiler>;