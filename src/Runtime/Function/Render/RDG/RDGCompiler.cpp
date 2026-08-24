#include "RDGCompiler.h"
#include "Function/Global/EngineContext.h"

void RDGCompiler::reset()
{
    passInfoAnalysis.reset_for_frame();

    passDependencyAnalysis.reset_for_frame();

    queueSchedule.reset_for_frame();

    executionReorder.reset_for_frame();

    crossQueueSyncAnalysis.reset_for_frame();

    passBinding.reset_for_frame();

    barrierGeneration.reset_for_frame();

    passExecution.reset_for_frame();
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
    
    // Phase 6: 绑定阶段——集中式资源分配 + 生命期分析 + 描述符集准备
    // （合并 SakuraEngine 的 resource_allocation / memory_aliasing(Tier0) / bind_table 三阶段）
    passBinding.on_execute(graph, executor);

    // Phase 7: 屏障生成——subresource级状态跟踪 + 拓扑序访问遍历，按pass分batch
    barrierGeneration.on_execute(graph, executor);

    // Phase 8: Pass执行——按拓扑序录制command，替代 RDGBuilder::Execute()
    passExecution.on_execute(graph, executor);
    
}