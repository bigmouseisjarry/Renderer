#include "pass_info_analysis.h"

#include "Function/Global/EngineContext.h"

void PassInfoAnalysis::reset_for_frame()
{
    pass_infos.clear();
    resource_infos.clear();
}

void PassInfoAnalysis::on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{
    ENGINE_TIME_SCOPE(PassInfoAnalysis::on_execute);

    auto& passes = get_passes(graph);
    pass_infos.reserve(passes.size());
    resource_infos.reserve(graph->ResourceNodeCount());
    for (RDGPassNodeRef pass : passes)
    {
        extract_pass_info(pass);
    }
}

const PassInfo* PassInfoAnalysis::get_pass_info(RDGPassNodeRef pass) const
{
    if (auto found = pass_infos.find(pass); found != pass_infos.end())
        return &found->second;
    return nullptr;
}

const ResourceInfo* PassInfoAnalysis::get_resource_info(RDGResourceNodeRef resource) const
{
    if (auto found = resource_infos.find(resource); found != resource_infos.end())
        return &found->second;
    return nullptr;
}

const PassResourceInfo* PassInfoAnalysis::get_resource_info(RDGPassNodeRef pass) const
{
    const auto* info = get_pass_info(pass);
    return info ? &info->resource_info : nullptr;
}

const PassPerformanceInfo* PassInfoAnalysis::get_performance_info(RDGPassNodeRef pass) const
{
    const auto* info = get_pass_info(pass);
    return info ? &info->performance_info : nullptr;
}

void PassInfoAnalysis::extract_pass_info(RDGPassNodeRef pass)
{
    PassInfo info;
    info.pass = pass;
    info.pass_type = pass->NodeType();

    extract_resource_info(pass, info.resource_info);
    extract_performance_info(pass, info.performance_info);

    pass_infos[pass] = info;
}

void PassInfoAnalysis::extract_resource_info(RDGPassNodeRef pass, PassResourceInfo& info)
{
    ENGINE_TIME_SCOPE(PassInfoAnalysis::extract_resource_info);

    info.resource_accesses.reserve(pass->buffers_count() + pass->textures_count());

    // Extract textures with detailed access info
    pass->foreach_textures([&](RDGTextureNodeRef texture, RDGTextureEdgeRef edge) {
        ResourceAccessInfo access_info;
        access_info.pass = pass;
        access_info.resource = texture;
        access_info.resource_state = edge->state;
        access_info.is_output = edge->IsOutput();

        // 从 edge flags 推断访问类型
        if (edge->asShaderReadWrite)       access_info.access_type = EResourceAccessType::ReadWrite;
        else if (edge->asColor || edge->asDepthStencil)
        {
            // loadOp == LOAD 表示 pass 既要读取先前内容又要写入，属于 ReadWrite
            if (edge->loadOp == ATTACHMENT_LOAD_OP_LOAD)
                access_info.access_type = EResourceAccessType::ReadWrite;
            else
                access_info.access_type = EResourceAccessType::Write;
        }
        else if (edge->asTransferDst)
            access_info.access_type = EResourceAccessType::Write;
        else                               access_info.access_type = EResourceAccessType::Read;

        // 子资源范围
        access_info.mip_base = edge->subresource.baseMipLevel;
        access_info.mip_count = edge->subresource.levelCount;
        access_info.array_base = edge->subresource.baseArrayLayer;
        access_info.array_count = edge->subresource.layerCount;
        info.resource_accesses.push_back(access_info);

        // 更新全局资源信息
        auto& global_resource_info = resource_infos[texture];
        global_resource_info.used_states.insert(std::make_pair(pass, access_info.resource_state));
        global_resource_info.memory_size = texture->get_size();

        // 根据 Pass 类型推断队列类型
        QueueType queue_type = QUEUE_TYPE_GRAPHICS;
        if (pass->NodeType() == RDGPassNodeType::RDG_PASS_NODE_TYPE_COMPUTE)
            queue_type = QUEUE_TYPE_COMPUTE;
        else if (pass->NodeType() == RDGPassNodeType::RDG_PASS_NODE_TYPE_COPY)
            queue_type = QUEUE_TYPE_TRANSFER;

        global_resource_info.add_queue(queue_type);
        info.total_resource_count += 1;

        return true;
        });

    // Extract buffers with detailed access info
    pass->foreach_buffers([&](RDGBufferNodeRef buffer, RDGBufferEdgeRef edge) {
        ResourceAccessInfo access_info;
        access_info.pass = pass;
        access_info.resource = buffer;
        access_info.resource_state = edge->state;
        access_info.is_output = edge->IsOutput();

        if (edge->asShaderReadWrite || edge->asOutputReadWrite)
            access_info.access_type = EResourceAccessType::ReadWrite;
        else
            access_info.access_type = EResourceAccessType::Read;

        access_info.buffer_from = edge->offset;
        access_info.buffer_to = edge->offset + edge->size;

        info.resource_accesses.push_back(access_info);

        // 更新全局资源信息
        auto& global_resource_info = resource_infos[buffer];
        global_resource_info.resource = buffer;
        global_resource_info.used_states.insert(std::make_pair(pass, access_info.resource_state));
        global_resource_info.memory_size = buffer->get_size();

        // 根据 Pass 类型推断队列类型
        QueueType queue_type = QUEUE_TYPE_GRAPHICS;
        if (pass->NodeType() == RDGPassNodeType::RDG_PASS_NODE_TYPE_COMPUTE)
            queue_type = QUEUE_TYPE_COMPUTE;
        else if (pass->NodeType() == RDGPassNodeType::RDG_PASS_NODE_TYPE_COPY)
            queue_type = QUEUE_TYPE_TRANSFER;

        global_resource_info.add_queue(queue_type);
        info.total_resource_count += 1;

        return true;
        });
}

void PassInfoAnalysis::extract_performance_info(RDGPassNodeRef pass, PassPerformanceInfo& info)
{
    // Direct flag reading only
    info.is_compute_intensive = pass->has_flags(RDGPassFlags::ComputeIntensive);
    info.is_bandwidth_intensive = pass->has_flags(RDGPassFlags::BandwidthIntensive);
    info.is_vertex_bound = pass->has_flags(RDGPassFlags::VertexBoundIntensive);
    info.is_pixel_bound = pass->has_flags(RDGPassFlags::PixelBoundIntensive);
    info.has_small_working_set = pass->has_flags(RDGPassFlags::SmallWorkingSet);
    info.has_large_working_set = pass->has_flags(RDGPassFlags::LargeWorkingSet);
    info.has_random_access = pass->has_flags(RDGPassFlags::RandomAccess);
    info.has_streaming_access = pass->has_flags(RDGPassFlags::StreamingAccess);
    info.prefers_async_compute = pass->has_flags(RDGPassFlags::PreferAsyncCompute);
    info.separate_command_buffer = pass->has_flags(RDGPassFlags::SeparateFromCommandBuffer);
}

// For dependency analysis - avoid recomputation
EResourceAccessType PassInfoAnalysis::get_resource_access_type(RDGPassNodeRef pass, RDGResourceNodeRef resource) const
{
    const auto* resource_info = get_resource_info(pass);
    if (!resource_info) return EResourceAccessType::Read;

    // Quick lookup in pre-computed access info
    for (const auto& access : resource_info->resource_accesses)
    {
        if (access.resource == resource)
        {
            return access.access_type;
        }
    }

    return EResourceAccessType::Read; // Default fallback
}

RHIResourceState PassInfoAnalysis::get_resource_state(RDGPassNodeRef pass, RDGResourceNodeRef resource) const
{
    if (auto it = resource_infos.find(resource); it != resource_infos.end())
    {
        if (it->second.used_states.contains(pass))
        {
            return it->second.used_states.find(pass)->second;
        }
    }
    return RESOURCE_STATE_UNDEFINED; // Default fallback
}