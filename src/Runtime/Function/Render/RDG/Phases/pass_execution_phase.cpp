#include "pass_execution_phase.h"

#include "Function/Global/EngineContext.h"
#include "Function/Global/EngineThreadPool.h"
#include "Function/Render/RDG/RDGPool.h"

#include <algorithm>

PassExecutionPhase::PassExecutionPhase(
    const QueueSchedule& queue_schedule,
    const ExecutionReorderPhase& reorder_phase,
    const CrossQueueSyncAnalysis& sync_analysis,
    const BarrierGenerationPhase& barrier_generation_phase,
    const PassBindingPhase& binding_phase,
    const CommandRecordingConfig& config)
    : config_(config)
    , queue_schedule_(queue_schedule)
    , reorder_phase_(reorder_phase)
    , sync_analysis_(sync_analysis)
    , barrier_generation_phase_(barrier_generation_phase)
    , binding_phase_(binding_phase)
{
}

void PassExecutionPhase::reset_for_frame()
{
    recording_result_.total_passes_executed.store(0, std::memory_order_relaxed);
    recording_result_.total_barriers_inserted.store(0, std::memory_order_relaxed);
    recording_result_.total_sync_points_processed = 0;
}

void PassExecutionPhase::on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{
    ENGINE_TIME_SCOPE(PassExecutionPhase::on_execute);

    // 同步点本期只做统计：信号量/时间线的实际插入是多队列提交期的工作（Sakura 亦如此）
    recording_result_.total_sync_points_processed =
        static_cast<uint32_t>(sync_analysis_.get_optimized_sync_points().size());

    execute_scheduled_passes(graph, executor);
}

// 核心执行流 ///////////////////////////////////////////////////////////////////

void PassExecutionPhase::execute_scheduled_passes(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{
    // 收集非culled的拓扑序pass（依赖保序的全局序列）。
    // 不能按 optimized_timeline 分队列整桶串接——跨队列的消费者可能被录制在生产者之前；
    // 分队列时间线留待多队列提交期配合同步点信号量消费
    const auto& topo_order = sync_analysis_.get_dependency_analysis().get_topological_order();
    std::vector<RDGPassNodeRef> orderedPasses;
    orderedPasses.reserve(topo_order.size());
    for (RDGPassNodeRef pass : topo_order)
        if (pass && !pass->isCulled) orderedPasses.push_back(pass);

    const uint32_t total = static_cast<uint32_t>(orderedPasses.size());
    if (total > 0)
    {
        const bool parallelReady =  executor->chunkCount >= 2
                                    && executor->ChunkCommands.size() >= executor->chunkCount
                                    && total >= 2;
        if (parallelReady)
        {
            record_parallel(graph, executor, orderedPasses);
        }
        else
        {
            // 串行：单命令流（延迟模式GraphicsCommand，Begin/End由BuildRDG/SubmitRHI负责）
            RHICommandListRef command = executor->GraphicsCommand;
            record_pass_range(graph, executor, command, 0, total, orderedPasses);
            executor->chunkCount = 0;   // 走旧的单流提交路径
        }
    }

    // 释放sweep：录制期不碰任何池（RDG池无锁），全部录制完成后按拓扑全序统一执行last-use释放
    release_sweep(graph, orderedPasses);


    if (config_.enable_debug_output)
    {
        // 安全网：帧结束时仍持有物理资源且未释放的非imported节点 = 泄漏源——
        // 该资源的池条目永远不会归还，下一帧必然新建一张，直到设备内存超限。
        // 正常情况（分配已被生命期过滤、last-use释放完整）此处不应有任何输出
        graph->ForEachTextureNode([&](RDGTextureNodeRef textureNode) {
            if (!textureNode->IsImported() && textureNode->texture != nullptr)
                ENGINE_LOG_WARN("[PassExecution] leaked texture '{}' ({}x{}, mip={}) never released",
                    textureNode->Name().c_str(),
                    textureNode->info.extent.width, textureNode->info.extent.height,
                    textureNode->info.mipLevels);
            });
        graph->ForEachBufferNode([&](RDGBufferNodeRef bufferNode) {
            if (!bufferNode->IsImported() && bufferNode->buffer != nullptr)
                ENGINE_LOG_WARN("[PassExecution] leaked buffer '{}' (size={}) never released",
                    bufferNode->Name().c_str(), bufferNode->info.size);
            });
    }

    // 尾声：池化描述符集必须等全部pass录制完再归还
    release_pooled_descriptor_sets(graph);

    if (config_.enable_debug_output)
    {
        ENGINE_LOG_INFO("[PassExecutionPhase] passes: {}, barriers: {}, sync points: {}",
            recording_result_.total_passes_executed.load(std::memory_order_relaxed),
            recording_result_.total_barriers_inserted.load(std::memory_order_relaxed),
            recording_result_.total_sync_points_processed);
    }
}

// 并行录制 /////////////////////////////////////////////////////////////////////

void PassExecutionPhase::record_parallel(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor,
                                         const std::vector<RDGPassNodeRef>& orderedPasses)
{
    const uint32_t total = static_cast<uint32_t>(orderedPasses.size());
    uint32_t chunkCount = std::min(executor->chunkCount, total);

    // 定位串行边界：serial_pass_names 首个命中位置 s——chunk边界只允许落在 [0, s) 内，
    // [s, total) 全部并入最后一个chunk，由主线程在workers join之后录制（时间上最后），
    // 保证 Editor UI（ImGui全局态 + 编辑器直写各pass的setting）与一切并发录制无重叠
    uint32_t serialBegin = total;
    if (!config_.serial_pass_names.empty())
    {
        for (uint32_t i = 0; i < serialBegin; i++)
        {
            for (const auto& name : config_.serial_pass_names)
            {
                if (orderedPasses[i]->Name() == name) { serialBegin = i; break; }
            }
            if (serialBegin != total) break;
        }
    }

    // worker chunk 数：总chunk数减尾chunk；不能超过串行边界前的pass数
    uint32_t workerChunks = chunkCount - 1;
    if (workerChunks > serialBegin) workerChunks = serialBegin;

    // 均分 [0, serialBegin) 给 worker chunks（余数并入最后一个worker chunk），
    // 尾chunk = [serialBegin, total)，由主线程录制
    const uint32_t share = workerChunks > 0 ? serialBegin / workerChunks : 0;

    // dispatch worker chunks（AddQueuedWork 的包装lambda会把队列时的帧号stamp到worker的
    // thread_local——lambda内ThreadFrameIndex()自动正确）
    for (uint32_t c = 0; c < workerChunks; c++)
    {
        uint32_t begin = c * share;
        uint32_t end = (c == workerChunks - 1) ? serialBegin : (c + 1) * share;
        if (begin >= end) continue;

        RHICommandListRef command = executor->ChunkCommands[c];
        EngineContext::ThreadPool()->AddQueuedWork([this, graph, executor, command, &orderedPasses, begin, end]()
        {
            command->BeginCommand();
            record_pass_range(graph, executor, command, begin, end, orderedPasses);
            command->EndCommand();
        });
    }

    // join：此刻ANY池没有其他任务，全池等待即等全部chunk录制完成
    EngineContext::ThreadPool()->WaitIdle(ENGINE_THREAD_TYPE_ANY);

    // 尾chunk主线程录（覆盖 [serialBegin, total)，含全部串行pass）——
    // 提交序仍在最后，录制时间上最后，与一切并发录制无重叠
    const uint32_t tailChunk = workerChunks;
    RHICommandListRef tailCommand = executor->ChunkCommands[tailChunk];
    tailCommand->BeginCommand();
    record_pass_range(graph, executor, tailCommand, serialBegin, total, orderedPasses);
    tailCommand->EndCommand();

    executor->chunkCount = tailChunk + 1;   // 提交 [0, tailChunk]
}

// 录制区间 /////////////////////////////////////////////////////////////////////

void PassExecutionPhase::record_pass_range(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RHICommandListRef command,
                                           uint32_t beginIndex, uint32_t endIndex, const std::vector<RDGPassNodeRef>& orderedPasses)
{
    for (uint32_t i = beginIndex; i < endIndex; i++)
        execute_pass(graph, executor, orderedPasses[i], command);
}

void PassExecutionPhase::execute_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGPassNodeRef pass, RHICommandListRef command)
{
    ENGINE_TIME_SCOPE_STR("PassExecutionPhase::" + pass->Name());

    // GPU调试标记（颜色与旧路径一致）
    if (config_.enable_debug_markers)
    {
        switch (pass->NodeType())
        {
        case RDG_PASS_NODE_TYPE_RENDER:      command->PushEvent(pass->Name(), {0.0f, 0.0f, 0.0f}); break;
        case RDG_PASS_NODE_TYPE_COMPUTE:     command->PushEvent(pass->Name(), {1.0f, 0.0f, 0.0f}); break;
        case RDG_PASS_NODE_TYPE_RAY_TRACING: command->PushEvent(pass->Name(), {0.0f, 1.0f, 0.0f}); break;
        case RDG_PASS_NODE_TYPE_PRESENT:     command->PushEvent(pass->Name(), {0.0f, 0.0f, 1.0f}); break;
        case RDG_PASS_NODE_TYPE_COPY:        command->PushEvent(pass->Name(), {1.0f, 1.0f, 0.0f}); break;
        default: break;
        }
    }

    // pass之前的转移屏障（非output边的pass期间状态）
    insert_pass_barriers(command, pass, false);

    switch (pass->NodeType())
    {
    case RDG_PASS_NODE_TYPE_RENDER:      execute_render_pass(graph, executor, dynamic_cast<RDGRenderPassNodeRef>(pass), command);      break;
    case RDG_PASS_NODE_TYPE_COMPUTE:     execute_compute_pass(graph, executor, dynamic_cast<RDGComputePassNodeRef>(pass), command);    break;
    case RDG_PASS_NODE_TYPE_RAY_TRACING: execute_ray_tracing_pass(graph, executor, dynamic_cast<RDGRayTracingPassNodeRef>(pass), command); break;
    case RDG_PASS_NODE_TYPE_PRESENT:     execute_present_pass(graph, executor, dynamic_cast<RDGPresentPassNodeRef>(pass), command);    break;
    case RDG_PASS_NODE_TYPE_COPY:        execute_copy_pass(graph, executor, dynamic_cast<RDGCopyPassNodeRef>(pass), command);          break;
    default:                             ENGINE_LOG_FATAL("Unsupported RDG pass type!");
    }

    // pass之后的收敛屏障（output边的产出状态）
    insert_pass_barriers(command, pass, true);

    if (config_.enable_debug_markers)
        command->PopEvent();

    // last-use释放不在录制期做（池无锁，不能并发）——由release_sweep在全部录制完成后统一执行

    recording_result_.total_passes_executed.fetch_add(1, std::memory_order_relaxed);
}

// 按类型执行 ///////////////////////////////////////////////////////////////////

void PassExecutionPhase::execute_render_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGRenderPassNodeRef pass, RHICommandListRef command)
{
    const PassBindInfo * bind_info = binding_phase_.get_pass_bind_info(pass);
    assert(bind_info != nullptr);
    command->BeginRendering(bind_info->rendering_info);

    RDGPassContext context = {
        .command = command,
        .builder = executor->builder,
        .descriptors = build_descriptor_array(pass)
    };
    context.passIndex[0] = pass->passIndex[0];
    context.passIndex[1] = pass->passIndex[1];
    context.passIndex[2] = pass->passIndex[2];
    pass->execute(context);

    command->EndRendering();

}

void PassExecutionPhase::execute_compute_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGComputePassNodeRef pass, RHICommandListRef command)
{
    RDGPassContext context = {
        .command = command,
        .builder = executor->builder,
        .descriptors = build_descriptor_array(pass)
    };
    context.passIndex[0] = pass->passIndex[0];
    context.passIndex[1] = pass->passIndex[1];
    context.passIndex[2] = pass->passIndex[2];
    pass->execute(context);
}

void PassExecutionPhase::execute_ray_tracing_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGRayTracingPassNodeRef pass, RHICommandListRef command)
{
    RDGPassContext context = {
        .command = command,
        .builder = executor->builder,
        .descriptors = build_descriptor_array(pass)
    };
    context.passIndex[0] = pass->passIndex[0];
    context.passIndex[1] = pass->passIndex[1];
    context.passIndex[2] = pass->passIndex[2];
    pass->execute(context);
}

void PassExecutionPhase::execute_present_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGPresentPassNodeRef pass, RHICommandListRef command)
{
    RDGTextureNodeRef presentTexture;
    RDGTextureNodeRef texture;
    TextureSubresourceLayers subresource;

    struct TextureEdgeInfo { RDGTextureEdgeRef edge; RDGTextureNodeRef node; };
    std::vector<TextureEdgeInfo> edges;
    graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef texNode) {
        edges.push_back({ edge, texNode });
        });

    if (edges[0].edge->asPresent)
    {
        presentTexture = edges[0].node;
        texture = edges[1].node;
        subresource = edges[1].edge->subresource.aspect == TEXTURE_ASPECT_NONE ?
            binding_phase_.get_texture(texture)->GetDefaultSubresourceLayers() : edges[1].edge->subresourceLayer;
    }
    else
    {
        presentTexture = edges[1].node;
        texture = edges[0].node;
        subresource = edges[0].edge->subresource.aspect == TEXTURE_ASPECT_NONE ?
            binding_phase_.get_texture(texture)->GetDefaultSubresourceLayers() : edges[0].edge->subresourceLayer;
    }

    // swapchain图像的手工屏障循环（pass内动态屏障，通用屏障由阶段7生成）
    command->TextureBarrier({binding_phase_.get_texture(presentTexture), RESOURCE_STATE_PRESENT, RESOURCE_STATE_TRANSFER_DST});
    command->CopyTexture(  binding_phase_.get_texture(texture), subresource,
                           binding_phase_.get_texture(presentTexture), {TEXTURE_ASPECT_COLOR, 0, 0, 1});
    command->TextureBarrier({binding_phase_.get_texture(presentTexture), RESOURCE_STATE_TRANSFER_DST, RESOURCE_STATE_PRESENT});
}

void PassExecutionPhase::execute_copy_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGCopyPassNodeRef pass, RHICommandListRef command)
{
    RDGBufferNodeRef bufferFrom = nullptr;
    RDGBufferNodeRef bufferTo = nullptr;
    uint32_t offsetFrom = 0;
    uint32_t offsetTo = 0;
    uint32_t size = 0;

    RDGTextureNodeRef textureFrom = nullptr;
    RDGTextureNodeRef textureTo = nullptr;
    TextureSubresourceLayers fromSubresource;
    TextureSubresourceLayers toSubresource;

    graph->ForEachBuffer(pass, [&](RDGBufferEdgeRef edge, RDGBufferNodeRef buffer) {
        if (edge->asTransferSrc)
        {
            bufferFrom = buffer;
            offsetFrom = edge->offset;
            size = edge->size;
        }
        else if (edge->asTransferDst)
        {
            bufferTo = buffer;
            offsetTo = edge->offset;
            size = edge->size;
        }
    });

    graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {
        if (edge->asTransferSrc)
        {
            textureFrom = texture;
            fromSubresource = edge->subresourceLayer;
        }
        else if (edge->asTransferDst)
        {
            textureTo = texture;
            toSubresource = edge->subresourceLayer;
        }
    });

    if(bufferFrom != nullptr && bufferTo != nullptr)
    {
        command->CopyBuffer( binding_phase_.get_buffer(bufferFrom), offsetFrom,
                            binding_phase_.get_buffer(bufferTo), offsetTo, size);
    }

    if(textureFrom != nullptr && textureTo != nullptr)
    {
        command->CopyTexture(   binding_phase_.get_texture(textureFrom), fromSubresource,
                                binding_phase_.get_texture(textureTo), toSubresource);

        if(pass->generateMip)      // mip链屏障是pass内动态屏障，保持手工发射
        {
            RHITextureBarrier barrier = {
                .texture = binding_phase_.get_texture(textureTo),
                .srcState = RESOURCE_STATE_TRANSFER_DST,
                .dstState = RESOURCE_STATE_TRANSFER_SRC,
                .subresource = {}
            };
            command->TextureBarrier(barrier);
            command->GenerateMips(binding_phase_.get_texture(textureTo)); // 默认纹理处于src状态，需要手动加屏障

            barrier = {
                .texture = binding_phase_.get_texture(textureTo),
                .srcState = RESOURCE_STATE_TRANSFER_SRC,
                .dstState = RESOURCE_STATE_TRANSFER_DST,
                .subresource = {}
            };
            command->TextureBarrier(barrier);
        }
    }
}

// 屏障发射 /////////////////////////////////////////////////////////////////////

void PassExecutionPhase::insert_pass_barriers(RHICommandListRef command, RDGPassNodeRef pass, bool after)
{
    const std::vector<BarrierBatch>* batches = barrier_generation_phase_.get_pass_barrier_batches(pass);
    if (batches == nullptr) return;

    for (const auto& batch : *batches)
    {
        for (const auto& barrier : batch.barriers)
        {
            if (barrier.after_pass != after) continue;
            if (barrier.resource == nullptr) continue;

            if (barrier.resource->NodeType() == RDG_RESOURCE_NODE_TYPE_TEXTURE)
            {
                command->TextureBarrier({
                    binding_phase_.get_texture(static_cast<RDGTextureNodeRef>(barrier.resource)),
                    barrier.before_state,
                    barrier.after_state,
                    barrier.subresource });
            }
            else
            {
                command->BufferBarrier({
                    binding_phase_.get_buffer(static_cast<RDGBufferNodeRef>(barrier.resource)),
                    barrier.before_state,
                    barrier.after_state,
                    barrier.buffer_offset,
                    barrier.buffer_size });
            }
            recording_result_.total_barriers_inserted.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

// 释放 /////////////////////////////////////////////////////////////////////////

void PassExecutionPhase::release_sweep(RDGDependencyGraphRef graph, const std::vector<RDGPassNodeRef>& orderedPasses)
{
    // 录制期不碰任何池（RDG池无锁，不能并发）——全部录制完成后按拓扑全序统一执行last-use释放。
    // 释放只是CPU侧池条目归还（下一帧分配才消费），时序后移不影响本帧GPU行为
    for (RDGPassNodeRef pass : orderedPasses)
        release_at_last_use(graph, pass);
}

void PassExecutionPhase::release_at_last_use(RDGDependencyGraphRef graph, RDGPassNodeRef pass)
{
    graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {
        if (binding_phase_.is_last_use(texture, pass, edge->IsOutput())) release_texture(texture, edge->state);
        });

    graph->ForEachBuffer(pass, [&](RDGBufferEdgeRef edge, RDGBufferNodeRef buffer) {
        if (binding_phase_.is_last_use(buffer, pass, edge->IsOutput())) release_buffer(buffer, edge->state);
        });

    if (const PassBindInfo* bind_info = binding_phase_.get_pass_bind_info(pass))
    {
        for (auto& view : bind_info->pooled_views)
            RDGTextureViewPool::Get()->Release({view});
    }
}

void PassExecutionPhase::release_texture(RDGTextureNodeRef textureNode, RHIResourceState state)
{
    if(textureNode->IsImported()) return;
    if(textureNode->texture)
    {
        RDGTexturePool::Get()->Release({ textureNode->texture, state});
        textureNode->texture = nullptr;
        textureNode->initState = RESOURCE_STATE_UNDEFINED;
    }
}

void PassExecutionPhase::release_buffer(RDGBufferNodeRef bufferNode, RHIResourceState state)
{
    if(bufferNode->IsImported()) return;
    if(bufferNode->buffer)
    {
        RDGBufferPool::Get()->Release({ bufferNode->buffer, state});
        bufferNode->buffer = nullptr;
        bufferNode->initState = RESOURCE_STATE_UNDEFINED;
    }
}

void PassExecutionPhase::release_pooled_descriptor_sets(RDGDependencyGraphRef graph)
{
    auto& passes = get_passes(graph);
    for (auto& pass : passes)
    {
        if (!pass) continue;

        const PassBindInfo* bind_info = binding_phase_.get_pass_bind_info(pass);
        if (bind_info == nullptr) continue;      // 池化的view在pass结束后就可以释放，但是描述符得全部执行完再释放？

        for (auto& descriptor : bind_info->pooled_descriptor_sets)
        {
            RDGDescriptorSetPool::Get(EngineContext::ThreadPool()->ThreadFrameIndex())
                ->Release({descriptor.first}, pass->rootSignature, descriptor.second);
        }
    }
}

// 工具 /////////////////////////////////////////////////////////////////////////

std::array<RHIDescriptorSetRef, MAX_DESCRIPTOR_SETS> PassExecutionPhase::build_descriptor_array(RDGPassNodeRef pass) const
{
    std::array<RHIDescriptorSetRef, MAX_DESCRIPTOR_SETS> descriptors = {};

    if (const PassBindInfo* bind_info = binding_phase_.get_pass_bind_info(pass))
    {
        for (const auto& [set, descriptor] : bind_info->descriptor_sets)
        {
            if (set < MAX_DESCRIPTOR_SETS) descriptors[set] = descriptor;
        }
    }

    return descriptors;
}
