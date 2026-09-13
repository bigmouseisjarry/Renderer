#include "RDGCompiler.h"
#include "Function/Global/EngineContext.h"
#include <cstdio>
void RDGCompiler::reset()
{

    ENGINE_TIME_SCOPE(RDGCompiler::reset);

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

    // Phase 3.5（HEFT协调步）：所有权链序边回写Phase2 + 按调度序重建拓扑——回写边是QFO
    // 所有权链的执行序载体与SSIS同步点来源；拓扑序以调度序为唯一权威（下游生命期/tracker
    // 假设"拓扑序=执行序"，Kahn哈希序与调度序不一致会造成生命期区间错位的间歇竞态）。
    // use_heft=false时无回写（旧路径的QFO执行序由Phase2静态边兜底）
    if (!queueSchedule.get_schedule_result().schedule_order.empty())
    {
        passDependencyAnalysis.add_scheduling_order_edges(queueSchedule.get_ownership_order_edges());
        passDependencyAnalysis.apply_schedule_order(queueSchedule.get_schedule_result().schedule_order);
    }

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