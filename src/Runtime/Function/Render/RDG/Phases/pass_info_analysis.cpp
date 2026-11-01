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

    // —— 默认值展开站——
    // 本phase虽名为Analysis，但承担一次性的"声明态→具体态"规范化：资源mipLevels(0=按extent推导)
    // 与边的subrange(levelCount/layerCount=0=全量、aspect=NONE=按format推导)在此全部展开并写回
    // 节点/边，此后下游（barrier/binding/execution/RHI发射）一律直读真值，多余的展开全部降级为断言。
    // aspect推导与物理纹理defaultRange同源（输入同为info.format，见VulkanRHITexture构造），
    // 不依赖物理资源分配时机。写回幂等（0→N只发生一次），Build已结束、改边安全
    graph->ForEachTextureNode([](RDGTextureNodeRef texture) {
        if (texture->info.mipLevels == 0)
        {
            assert(texture->info.extent.width != 0 || texture->info.extent.height != 0 || texture->info.extent.depth != 0);
            texture->info.mipLevels = texture->info.extent.MipSize();
        }
    });

    auto& passes = get_passes(graph);
    pass_infos.reserve(passes.size());
    resource_infos.reserve(graph->ResourceNodeCount());
    for (RDGPassNodeRef pass : passes)
    {
        // 对每一个pass做分析
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

    // 获取pass的资源访问信息
    extract_resource_info(pass, info.resource_info);
    // 获取pass的偏向
    extract_performance_info(pass, info.performance_info);

    pass_infos[pass] = info;
}

void PassInfoAnalysis::extract_resource_info(RDGPassNodeRef pass, PassResourceInfo& info)
{
    ENGINE_TIME_SCOPE(PassInfoAnalysis::extract_resource_info);

    info.resource_accesses.reserve(pass->buffers_count() + pass->textures_count());

    // pass访问的每一条texture边
    pass->foreach_textures([&](RDGTextureNodeRef texture, RDGTextureEdgeRef edge) {
        ResourceAccessInfo access_info;
        access_info.pass = pass;
        access_info.resource = texture;
        access_info.resource_state = edge->state;
        access_info.frequency = edge->frequency;

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
        else                               
            access_info.access_type = EResourceAccessType::Read;

        // 子资源范围——默认值展开站（on_execute注释）：count=0→节点全量，aspect=NONE→format推导。
        // 展开写回边（幂等），后续所有消费者直读真值；例外=generateMip的To边只扩access不写回边（见下方）
        TextureSubresourceRange& sub = edge->subresource;
        if (sub.levelCount == 0) sub.levelCount = texture->info.mipLevels;
        if (sub.layerCount == 0) sub.layerCount = texture->info.arrayLayers;
        if (sub.aspect == TEXTURE_ASPECT_NONE)
            sub.aspect = IsDepthStencilFormat(texture->info.format) ? TEXTURE_ASPECT_DEPTH_STENCIL :
                         IsDepthFormat(texture->info.format)        ? TEXTURE_ASPECT_DEPTH :
                         IsStencilFormat(texture->info.format)      ? TEXTURE_ASPECT_STENCIL :
                                                                      TEXTURE_ASPECT_COLOR;
        access_info.aspect = sub.aspect;
        access_info.mip_base = sub.baseMipLevel;
        access_info.mip_count = sub.levelCount;
        access_info.array_base = sub.baseArrayLayer;
        access_info.array_count = sub.layerCount;

        // generateMip例外：access扩全链（GenerateMips实际触达0..M-1×全layer，与copy单mip无关），
        // 边保持单mip冻结（RHI copy族单mip契约，execution侧回转Layers直读边）。不扩则mip 1..M-1
        // 帧内零屏障，执行期手工mip链屏障（DST→SRC/SRC→DST全纹理声称）对真实旧态
        // （首帧UNDEFINED/稳态SHADER_READ）撒谎——"expects TRANSFER_DST / current UNDEFINED"报错族病根
        if (edge->asTransferDst && pass->NodeType() == RDG_PASS_NODE_TYPE_COPY &&
            static_cast<RDGCopyPassNodeRef>(pass)->IsGenerateMip())
        {
            access_info.mip_base = 0;
            access_info.mip_count = texture->info.mipLevels;
            access_info.array_base = 0;
            access_info.array_count = texture->info.arrayLayers;
        }

        info.resource_accesses.push_back(access_info);

        // 更新全局资源信息
        ResourceInfo& global_resource_info = resource_infos[texture];
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

    // pass访问的每一条buffer边
    pass->foreach_buffers([&](RDGBufferNodeRef buffer, RDGBufferEdgeRef edge) {
        ResourceAccessInfo access_info;
        access_info.pass = pass;
        access_info.resource = buffer;
        access_info.resource_state = edge->state;
        access_info.frequency = edge->frequency;

        if (edge->asShaderReadWrite)
            access_info.access_type = EResourceAccessType::ReadWrite;
        else
            access_info.access_type = EResourceAccessType::Read;

        access_info.buffer_from = edge->offset;
        access_info.buffer_to = edge->offset + edge->size;

        info.resource_accesses.push_back(access_info);

        // 更新全局资源信息
        ResourceInfo& global_resource_info = resource_infos[buffer];
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

// 
EResourceAccessType PassInfoAnalysis::get_resource_access_type(RDGPassNodeRef pass, RDGResourceNodeRef resource) const
{
    const PassResourceInfo* resource_info = get_resource_info(pass);
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