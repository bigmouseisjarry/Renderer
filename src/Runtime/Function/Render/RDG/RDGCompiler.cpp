#include "RDGCompiler.h"
#include "Function/Global/EngineContext.h"

void RDGCompiler::compile_and_execute(RDGDependencyGraphRef graph, RDGPerFrameResource* executor)
{
    ENGINE_TIME_SCOPE(RDGCompiler::compile_and_execute);

    // Phase 1: 收集 pass 信息、资源访问模式、性能提示
    passInfoAnalysis.on_execute(graph, executor);

    // Phase 2: 依赖分析 + 拓扑排序（后续实现）
    // dependencyAnalysis.on_execute(graph, executor);

    // Phase 3: 队列调度（后续实现）
    // queueSchedule.on_execute(graph, executor);

    // ...
    // Phase N: PassExecutionPhase — 实际录制和提交 command
    //         这一步替代现有的 RDGBuilder::Execute()
}