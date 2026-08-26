#include "pass_binding_phase.h"

#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGPool.h"

#include <algorithm>

PassBindingPhase::PassBindingPhase(
    const PassInfoAnalysis& pass_info_analysis,
    const CrossQueueSyncAnalysis& sync_analysis,
    const PassBindingConfig& config)
    : config_(config)
    , pass_info_analysis_(pass_info_analysis)
    , sync_analysis_(sync_analysis)
{
}

void PassBindingPhase::reset_for_frame()
{
    pass_bind_infos_.clear();
    lifecycle_.last_use.clear();
}

void PassBindingPhase::on_execute(RDGDependencyGraphRef graph, PerFrameCommonResourceRef executor)
{
    ENGINE_TIME_SCOPE(PassBindingPhase::on_execute);

    // 顺序固定：先做生命期分析（确定哪些资源真正被使用，供分配过滤与执行期释放），
    // 再集中分配物理资源（只分配有使用记录的节点），最后准备描述符集（供执行期组装 RDGPassContext）
    analyze_resource_lifetime(graph);
    allocate_resources(graph);
    prepare_descriptor_sets(graph);

    if (config_.enable_debug_output)
    {
        debug_info();
    }
}

RHITextureRef PassBindingPhase::get_texture(RDGTextureNodeRef texture) const
{
    return texture->texture;
}

RHIBufferRef PassBindingPhase::get_buffer(RDGBufferNodeRef buffer) const
{
    return buffer->buffer;
}

const PassBindInfo* PassBindingPhase::get_pass_bind_info(RDGPassNodeRef pass) const
{
    if (auto found = pass_bind_infos_.find(pass); found != pass_bind_infos_.end())
        return &found->second;
    return nullptr;
}

bool PassBindingPhase::is_last_use(RDGResourceNodeRef resource, RDGPassNodeRef pass, bool output) const
{
    auto found = lifecycle_.last_use.find(resource);
    if (found == lifecycle_.last_use.end()) return false;

    const ResourceLifecycleInfo::LastUse& last_use = found->second;
    if (last_use.pass != pass) return false;

    // 旧 IsLastUsedPass 的规则：同一pass内先读后写（读边释放时本pass还有output边）不算最后使用
    if (!output && last_use.pass_has_output_edge) return false;

    return true;
}

const void PassBindingPhase::debug_info() const
{
    ENGINE_LOG_INFO("start PassBindingPhase");

    // ===== pass_bind_infos_：每个pass的绑定结果 =====
    ENGINE_LOG_INFO("[PassBinding] pass_bind_infos_: {} passes", pass_bind_infos_.size());

    for (const auto& [pass, bind_info] : pass_bind_infos_)
    {
        ENGINE_LOG_INFO("[PassBinding]   pass '{}': rootsig={}, sets={}, pooled_views={}, pooled_sets={}",
            pass->Name().c_str(),
            pass->rootSignature != nullptr,
            bind_info.descriptor_sets.size(),
            bind_info.pooled_views.size(),
            bind_info.pooled_descriptor_sets.size());

        // set索引排序输出，保证帧间可比（unordered_map遍历序不稳定）
        std::vector<uint32_t> set_indices;
        set_indices.reserve(bind_info.descriptor_sets.size());
        for (const auto& [set, descriptor] : bind_info.descriptor_sets)
            set_indices.push_back(set);
        std::sort(set_indices.begin(), set_indices.end());

        for (uint32_t set : set_indices)
        {
            // 输出裸指针：跨帧相同指针 = 描述符池复用生效
            ENGINE_LOG_INFO("[PassBinding]     set {} -> descriptor {}",
                set, (void*)bind_info.descriptor_sets.at(set).get());
        }
    }

    // ===== lifecycle_：资源生命期（last_use） =====
    ENGINE_LOG_INFO("[PassBinding] lifecycle_: {} resources", lifecycle_.last_use.size());

    for (const auto& [resource, last_use] : lifecycle_.last_use)
    {
        ENGINE_LOG_INFO("[PassBinding]   {} '{}' -> last use '{}' (has_output_edge={})",
            resource->NodeType() == RDG_RESOURCE_NODE_TYPE_TEXTURE ? "texture" : "buffer",
            resource->Name().c_str(),
            last_use.pass != nullptr ? last_use.pass->Name().c_str() : "null",
            last_use.pass_has_output_edge);
    }

    ENGINE_LOG_INFO("end PassBindingPhase");
}

// Step 2: 资源预分配 ///////////////////////////////////////////////////////////////////

void PassBindingPhase::allocate_resources(RDGDependencyGraphRef graph)
{
    // 原 RDGBuilder::Resolve 的集中化版本：帧首一次性为所有（非imported）虚拟节点分配物理资源，
    // 池条目携带的跨帧状态写入 node->initState，供后续屏障生成与描述符准备使用。
    // imported 节点在 Build 期已持有资源指针，此处天然跳过
    graph->ForEachTextureNode([&](RDGTextureNodeRef textureNode) {
        if (textureNode->texture != nullptr) return;                        // imported或已分配
        if (!lifecycle_.last_use.contains(textureNode))
        {
            // 孤儿节点：声明了但没有任何pass连边使用。不分配（对齐旧路径的惰性Resolve）——
            // 若分配了将没有任何pass触发last-use释放，纹理池每帧净漏一张直到超限
            if (config_.enable_debug_output)
                ENGINE_LOG_INFO("[PassBinding] orphan texture '{}' skipped (no using pass)", textureNode->Name().c_str());
            return;
        }

        auto pooledTexture = RDGTexturePool::Get()->Allocate(textureNode->info);
        textureNode->texture = pooledTexture.texture;
        textureNode->initState = pooledTexture.state;
        // 同步解析后的info：节点声明里 mipLevels=0 表示自动推导，池内创建时被解析为实际值
        // （如 extent.MipSize()），必须回写节点，否则下游（如屏障生成的subresource跟踪器）会维度错配
        textureNode->info = pooledTexture.texture->GetInfo();
    });

    graph->ForEachBufferNode([&](RDGBufferNodeRef bufferNode) {
        if (bufferNode->buffer != nullptr) return;
        if (!lifecycle_.last_use.contains(bufferNode))
        {
            if (config_.enable_debug_output)
                ENGINE_LOG_INFO("[PassBinding] orphan buffer '{}' skipped (no using pass)", bufferNode->Name().c_str());
            return;
        }

        auto pooledBuffer = RDGBufferPool::Get()->Allocate(bufferNode->info);
        bufferNode->buffer = pooledBuffer.buffer;
        bufferNode->initState = pooledBuffer.state;
        // 同理：池可能返回size更大的buffer，同步为物理资源实际的info
        bufferNode->info = pooledBuffer.buffer->GetInfo();
    });
}

// Step 1: 生命期分析 ///////////////////////////////////////////////////////////////////

void PassBindingPhase::analyze_resource_lifetime(RDGDependencyGraphRef graph)
{
    // 以 Phase 2 的逻辑拓扑序作为全局执行序（依赖保序，跨队列调度/重排都不会违反它），
    // 每个资源的 last_use = 其所有使用pass中拓扑位置最大者。
    const std::vector<RDGPassNodeRef>& topo_order = sync_analysis_.get_dependency_analysis().get_topological_order();

    std::unordered_map<RDGPassNodeRef, uint32_t> topo_position;
    topo_position.reserve(topo_order.size());
    for (uint32_t i = 0; i < topo_order.size(); i++)
        topo_position[topo_order[i]] = i;

    graph->ForEachTextureNode([&](RDGTextureNodeRef texture) {
        ResourceLifecycleInfo::LastUse last_use;
        uint32_t best_position = 0;

        // 遍历该texture相关的每一个pass
        graph->ForEachPass(texture, [&](RDGTextureEdgeRef edge, RDGPassNodeRef pass) {
            auto found = topo_position.find(pass);
            if (found == topo_position.end()) return;   // 不在拓扑序中，理论上不会发生

            // 第一次记录last_use或者这个pass的拓扑序位置大于前面记录的最大拓扑序位置
            if (last_use.pass == nullptr || found->second > best_position)
            {
                best_position = found->second;
                last_use.pass = pass;
                last_use.pass_has_output_edge = edge->IsOutput();
            }
            else if (pass == last_use.pass && edge->IsOutput())
            {
                last_use.pass_has_output_edge = true;   // 同一pass内的output边（先读后写）
            }
        });

        if (last_use.pass != nullptr)
            lifecycle_.last_use[texture] = last_use;
    });

    graph->ForEachBufferNode([&](RDGBufferNodeRef buffer) {
        ResourceLifecycleInfo::LastUse last_use;
        uint32_t best_position = 0;

        graph->ForEachPass(buffer, [&](RDGBufferEdgeRef edge, RDGPassNodeRef pass) {
            auto found = topo_position.find(pass);
            if (found == topo_position.end()) return;

            if (last_use.pass == nullptr || found->second > best_position)
            {
                best_position = found->second;
                last_use.pass = pass;
                last_use.pass_has_output_edge = edge->IsOutput();
            }
            else if (pass == last_use.pass && edge->IsOutput())
            {
                last_use.pass_has_output_edge = true;
            }
        });

        if (last_use.pass != nullptr)
            lifecycle_.last_use[buffer] = last_use;
    });
}

// Step 3: 描述符集准备 /////////////////////////////////////////////////////////////////

void PassBindingPhase::prepare_descriptor_sets(RDGDependencyGraphRef graph)
{
    std::vector<RDGPassNodeRef>& passes = get_passes(graph);
    pass_bind_infos_.reserve(passes.size());

    for (RDGPassNodeRef pass : passes)
    {
        if (!pass || pass->isCulled) continue;

        // Present/Copy 不需要描述符集/视图（CopyTexture 用 subresource layers 直取纹理），
        // 旧路径的 ExecutePass(present/copy) 也从不调用 PrepareDescriptorSet
        const RDGPassNodeType pass_type = pass->NodeType();
        if (pass_type == RDG_PASS_NODE_TYPE_PRESENT || pass_type == RDG_PASS_NODE_TYPE_COPY) continue;

        PassBindInfo& bind_info = pass_bind_infos_[pass];

        // 预置显式传入的描述符集（RDGRenderPassBuilder::DescriptorSet 等），保持与旧路径一致：
        // 显式提供的set不参与池化分配，也不在本阶段更新
        for (uint32_t set = 0; set < MAX_DESCRIPTOR_SETS; set++)
        {
            if (pass->descriptorSets[set] != nullptr)
                bind_info.descriptor_sets[set] = pass->descriptorSets[set];
        }

        // 惰性分配指定set的池化描述符集
        auto ensure_descriptor_set = [&](uint32_t set) -> RHIDescriptorSetRef {
            auto found = bind_info.descriptor_sets.find(set);
            if (found != bind_info.descriptor_sets.end() && found->second != nullptr)
                return found->second;
            if (pass->rootSignature == nullptr)
                return nullptr;

            RHIDescriptorSetRef descriptor = RDGDescriptorSetPool::Get(EngineContext::ThreadPool()->ThreadFrameIndex())
                                  ->Allocate(pass->rootSignature, set).descriptor;
            bind_info.descriptor_sets[set] = descriptor;
            bind_info.pooled_descriptor_sets.push_back({descriptor, set});
            return descriptor;
        };

        graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {

            if (edge->IsOutput()) return;    // 作为output声明时不需要view

            RHITextureViewRef view = RDGTextureViewPool::Get()->Allocate({
            .texture = texture->texture,    // Step1 已完成集中分配
            .format = texture->info.format,
            .viewType = edge->viewType,
            .subresource = edge->subresource }).textureView;
            bind_info.pooled_views.push_back(view);

            // rendering target：view与RHIRenderingInfo在主线程预构建（原执行期PrepareRenderingTarget），
            // 并行录制期worker不碰无锁的视图池；view随pooled_views由释放sweep统一归还
            if (edge->asColor || edge->asDepthStencil)
            {

                //RHITextureViewRef view = RDGTextureViewPool::Get()->Allocate({
                //.texture = texture->texture,
                //.format = texture->info.format,
                //.viewType = edge->viewType,
                //.subresource = edge->subresource }).textureView;
                //bind_info.pooled_views.push_back(view);

                RHIRenderingInfo& renderingInfo = bind_info.rendering_info;
                renderingInfo.multiviewCount = static_cast<RDGRenderPassNodeRef>(pass)->multiviewCount;
                renderingInfo.extent = { texture->info.extent.width, texture->info.extent.height };
                renderingInfo.layers = renderingInfo.multiviewCount > 0 ? 1 :                  // 启用 multiview时，textureview的layer还是多个，framebuffer强制为1
                    edge->subresource.layerCount > 0 ? edge->subresource.layerCount : 1;

                if (edge->asColor)
                {
                    renderingInfo.colorAttachments[edge->binding] = {
                    .textureView = view,
                    .currentState = edge->state,
                    .loadOp = edge->loadOp,
                    .storeOp = edge->storeOp,
                    .clearColor = edge->clearColor,
                    };
                }
                else
                {
                    renderingInfo.depthStencilAttachment = {
                    .textureView = view,
                    .currentState = edge->state,
                    .loadOp = edge->loadOp,
                    .storeOp = edge->storeOp,
                    .clearDepth = edge->clearDepth,
                    .clearStencil = edge->clearStencil
                    };
                }
                return;
            }

            RHIDescriptorSetRef descriptor = ensure_descriptor_set(edge->set);

            if((edge->asShaderRead || edge->asShaderReadWrite) && descriptor != nullptr)
            {
                RHIDescriptorUpdateInfo updateInfo = {
                    .binding = edge->binding,
                    .index = edge->index,
                    .resourceType = edge->type,
                    .textureView = view
                };
                descriptor->UpdateDescriptor(updateInfo);
            }
        });

        graph->ForEachBuffer(pass, [&](RDGBufferEdgeRef edge, RDGBufferNodeRef buffer) {

            if (edge->set == NO_DESCRIPTOR_SET) return;    // 虚拟依赖边：只表达排序/屏障，不绑定描述符

            RHIDescriptorSetRef descriptor = ensure_descriptor_set(edge->set);

            if((edge->asShaderRead || edge->asShaderReadWrite) && descriptor != nullptr)
            {
                RHIDescriptorUpdateInfo updateInfo = {
                    .binding = edge->binding,
                    .index = edge->index,
                    .resourceType = edge->type,
                    .buffer = buffer->buffer,    // Step1 已完成集中分配
                    .bufferOffset = edge->offset,
                    .bufferRange = edge->size
                };

                descriptor->UpdateDescriptor(updateInfo);
            }
        });

        for(auto& sampler : pass->samplers)
        {
            RHIDescriptorSetRef descriptor = ensure_descriptor_set(sampler.set);

            if(descriptor != nullptr)
            {
                RHIDescriptorUpdateInfo updateInfo = {
                    .binding = sampler.binding,
                    .index = sampler.index,
                    .resourceType = RESOURCE_TYPE_SAMPLER,
                    .sampler = sampler.sampler
                };

                descriptor->UpdateDescriptor(updateInfo);
            }
        }
    }
}
