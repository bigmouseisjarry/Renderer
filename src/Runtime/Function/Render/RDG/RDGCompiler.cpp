#include "RDGCompiler.h"
#include "Function/Global/EngineContext.h"

void RDGCompiler::reset()
{
    passInfoAnalysis.reset_for_frame();

    passDependencyAnalysis.reset_for_frame();

    queueSchedule.reset_for_frame();

    executionReorder.reset_for_frame();

    crossQueueSyncAnalysis.reset_for_frame();
}

void RDGCompiler::compile_and_execute(RDGDependencyGraphRef graph, RDGPerFrameResource* executor)
{
    ENGINE_TIME_SCOPE(RDGCompiler::compile_and_execute);

    reset();

    // Phase 1: 收集 pass 信息、资源访问模式、性能提示
    passInfoAnalysis.on_execute(graph, executor);

    // Phase 2: 依赖分析 + 拓扑排序
    passDependencyAnalysis.on_execute(graph, executor);

    // Phase 3: 队列调度
    queueSchedule.on_execute(graph, executor);

    // Phase 4: 队列调度优化
    executionReorder.on_execute(graph, executor);

    // Phase 5: 同步点生成
    crossQueueSyncAnalysis.on_execute(graph, executor);

    // ...
    // Phase N: PassExecutionPhase — 实际录制和提交 command
    //         这一步替代现有的 RDGBuilder::Execute()
}