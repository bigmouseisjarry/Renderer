#include "pass_execution_phase.h"

#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGPool.h"

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
    recording_result_ = {};
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
    // 按Phase 2拓扑序单命令流录制（依赖保序）。
    // 不能按 optimized_timeline 分队列整桶串接——跨队列的消费者可能被录制在生产者之前；
    // 分队列时间线留待多队列提交期配合同步点信号量消费
    const auto& topo_order = sync_analysis_.get_dependency_analysis().get_topological_order();

    for (RDGPassNodeRef pass : topo_order)
    {
        if (!pass || pass->isCulled) continue;
        execute_pass(graph, executor, pass);
    }

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

    // 尾声：池化描述符集必须等全部pass录制完再归还（对应旧 Execute() 的尾循环）
    release_pooled_descriptor_sets(graph);

    if (config_.enable_debug_output)
    {
        ENGINE_LOG_INFO("[PassExecutionPhase] passes: {}, barriers: {}, sync points: {}",
            recording_result_.total_passes_executed,
            recording_result_.total_barriers_inserted,
            recording_result_.total_sync_points_processed);
    }
}

void PassExecutionPhase::execute_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGPassNodeRef pass)
{
    ENGINE_TIME_SCOPE_STR("PassExecutionPhase::" + pass->Name());

    RHICommandListRef command = executor->GraphicsCommand;

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
    insert_pass_barriers(executor, pass, false);

    switch (pass->NodeType())
    {
    case RDG_PASS_NODE_TYPE_RENDER:      execute_render_pass(graph, executor, dynamic_cast<RDGRenderPassNodeRef>(pass));      break;
    case RDG_PASS_NODE_TYPE_COMPUTE:     execute_compute_pass(graph, executor, dynamic_cast<RDGComputePassNodeRef>(pass));    break;
    case RDG_PASS_NODE_TYPE_RAY_TRACING: execute_ray_tracing_pass(graph, executor, dynamic_cast<RDGRayTracingPassNodeRef>(pass)); break;
    case RDG_PASS_NODE_TYPE_PRESENT:     execute_present_pass(graph, executor, dynamic_cast<RDGPresentPassNodeRef>(pass));    break;
    case RDG_PASS_NODE_TYPE_COPY:        execute_copy_pass(graph, executor, dynamic_cast<RDGCopyPassNodeRef>(pass));          break;
    default:                             ENGINE_LOG_FATAL("Unsupported RDG pass type!");
    }

    // pass之后的收敛屏障（output边的产出状态）
    insert_pass_barriers(executor, pass, true);

    // last-use资源释放（生命期来自阶段6）
    release_at_last_use(graph, pass);

    if (config_.enable_debug_markers)
        command->PopEvent();

    recording_result_.total_passes_executed++;
}

// 按类型执行 ///////////////////////////////////////////////////////////////////

void PassExecutionPhase::execute_render_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGRenderPassNodeRef pass)
{
    RHICommandListRef command = executor->GraphicsCommand;

    RHIRenderingInfo renderingInfo = {};
    std::vector<RHITextureViewRef> attachmentViews;     // 本pass的attachment view，pass结束即归还视图池
    prepare_rendering_target(graph, pass, renderingInfo, attachmentViews);

    command->BeginRendering(renderingInfo);

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

    for (auto& view : attachmentViews)
        RDGTextureViewPool::Get()->Release({view});
}

void PassExecutionPhase::execute_compute_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGComputePassNodeRef pass)
{
    RHICommandListRef command = executor->GraphicsCommand;

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

void PassExecutionPhase::execute_ray_tracing_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGRayTracingPassNodeRef pass)
{
    RHICommandListRef command = executor->GraphicsCommand;

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

void PassExecutionPhase::execute_present_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGPresentPassNodeRef pass)
{
    RHICommandListRef command = executor->GraphicsCommand;

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

void PassExecutionPhase::execute_copy_pass(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor, RDGCopyPassNodeRef pass)
{
    RHICommandListRef command = executor->GraphicsCommand;

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

void PassExecutionPhase::insert_pass_barriers(PerFrameCommonResourceRef executor, RDGPassNodeRef pass, bool after)
{
    const std::vector<BarrierBatch>* batches = barrier_generation_phase_.get_pass_barrier_batches(pass);
    if (batches == nullptr) return;

    RHICommandListRef command = executor->GraphicsCommand;

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
            recording_result_.total_barriers_inserted++;
        }
    }
}

// rendering target 准备 ////////////////////////////////////////////////////////

void PassExecutionPhase::prepare_rendering_target(RDGDependencyGraphRef graph, RDGRenderPassNodeRef pass, RHIRenderingInfo& renderingInfo,
                                                  std::vector<RHITextureViewRef>& attachmentViews)
{
    renderingInfo.multiviewCount = pass->multiviewCount;

    graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {

        if (edge->IsOutput()) return;                            // 作为output声明时不需要view
        if (!(edge->asColor || edge->asDepthStencil)) return;    // rendering target单独处理
        RHITextureViewRef view = RDGTextureViewPool::Get()->Allocate({
            .texture = binding_phase_.get_texture(texture),
            .format = texture->info.format,
            .viewType = edge->viewType,
            .subresource = edge->subresource }).textureView;
        attachmentViews.push_back(view);

        if (edge->asColor)
        {
            renderingInfo.extent = { texture->info.extent.width, texture->info.extent.height };
            renderingInfo.layers = pass->multiviewCount > 0 ? 1 :                      // 启用multiview特性时，textureview的layer还是多个，framebuffer强制为1
                edge->subresource.layerCount > 0 ? edge->subresource.layerCount : 1;

            renderingInfo.colorAttachments[edge->binding] = {
                .textureView = view,
                .currentState = edge->state,
                .loadOp = edge->loadOp,
                .storeOp = edge->storeOp,
                .clearColor = edge->clearColor,
            };
        }
        else if (edge->asDepthStencil)
        {
            renderingInfo.extent = { texture->info.extent.width, texture->info.extent.height };
            renderingInfo.layers = pass->multiviewCount > 0 ? 1 :
                edge->subresource.layerCount > 0 ? edge->subresource.layerCount : 1;

            renderingInfo.depthStencilAttachment = {
                .textureView = view,
                .currentState = edge->state,
                .loadOp = edge->loadOp,
                .storeOp = edge->storeOp,
                .clearDepth = edge->clearDepth,
                .clearStencil = edge->clearStencil
            };
        }
        });
}

// 释放 /////////////////////////////////////////////////////////////////////////

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
