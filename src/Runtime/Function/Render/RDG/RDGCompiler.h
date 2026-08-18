#pragma once

#include "Function/Render/RDG/RDGNode.h"
#include "Function/Render/RDG/Phases/IRenderGraphPhase.h"
#include "Function/Render/RDG/Phases/pass_info_analysis.h"

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

    void compile_and_execute(RDGDependencyGraphRef graph, RDGPerFrameResource* executor);

    const PassInfoAnalysis& GetPassInfoAnalysis() const { return passInfoAnalysis; }

private:
    // ---- Phase 实例 ----
    // 阶段 1: 信息收集
    PassInfoAnalysis passInfoAnalysis;

};
using RDGCompilerRef = std::shared_ptr<RDGCompiler>;