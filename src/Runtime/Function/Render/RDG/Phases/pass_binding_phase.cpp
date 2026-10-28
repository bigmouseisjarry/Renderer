#include "pass_binding_phase.h"

#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGPool.h"

#include <algorithm>
#include <cassert>
#include <utility>

PassBindingPhase::PassBindingPhase(
    const PassInfoAnalysis& pass_info_analysis,
    const QueueSchedule& queue_schedule,
    const PassBindingConfig& config)
    : config_(config)
    , pass_info_analysis_(pass_info_analysis)
    , queue_schedule_(queue_schedule)
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
            if (config_.enable_debug_output)
                ENGINE_LOG_INFO("[PassBinding] orphan texture '{}' skipped (no using pass)", textureNode->Name().c_str());
            return;
        }

        auto pooledTexture = RDGTexturePool::Get(EngineContext::ThreadPool()->ThreadFrameIndex())->Allocate(textureNode->info);
        textureNode->texture = pooledTexture.texture;
        textureNode->initState = pooledTexture.state;
        textureNode->initFamily = pooledTexture.queueFamily;    // 池化跨帧归属族（阶段7帧首acquire判定）
        assert(textureNode->info == pooledTexture.texture->GetInfo());
    });

    graph->ForEachBufferNode([&](RDGBufferNodeRef bufferNode) {
        if (bufferNode->buffer != nullptr) return;
        if (!lifecycle_.last_use.contains(bufferNode))
        {
            if (config_.enable_debug_output)
                ENGINE_LOG_INFO("[PassBinding] orphan buffer '{}' skipped (no using pass)", bufferNode->Name().c_str());
            return;
        }

        auto pooledBuffer = RDGBufferPool::Get(EngineContext::ThreadPool()->ThreadFrameIndex())->Allocate(bufferNode->info);
        bufferNode->buffer = pooledBuffer.buffer;
        bufferNode->initState = pooledBuffer.state;
        // 池Key仅{usage,type,flag}，size够大即复用，物理size可能大于声明——
        // 与纹理路径（精确匹配仅断言）不同，此处回写是承重的，勿删
        bufferNode->info = pooledBuffer.buffer->GetInfo();
    });
}

// Step 1: 生命期分析 ///////////////////////////////////////////////////////////////////

void PassBindingPhase::analyze_resource_lifetime(RDGDependencyGraphRef graph)
{
    // 以 HEFT 全局调度序作为全局执行序（调度序=执行序；与 Phase 3.5 回写进依赖分析的
    // 权威拓扑同源），每个资源的 last_use = 其所有使用pass中调度位置最大者。
    const std::vector<RDGPassNodeRef>& topo_order = queue_schedule_.get_schedule_result().schedule_order;

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

            // 第一次记录last_use或者这个pass的执行序位置大于前面记录的最大执行序位置
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

    // 遍历每一个pass
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

            // set2=足迹键(used entities分桶,S4定案);set0/1=旧键(★set1巨型bindless必须单桶,见RDGPool.h)
            // 10-27晚消融已毕:两种键均与Xid概率崩无关(案件转环境假说)
            RHIDescriptorSetRef descriptor = RDGDescriptorSetPool::Get(EngineContext::ThreadPool()->ThreadFrameIndex())
                ->Allocate(set == 2 ? RDGDescriptorSetPool::Key::MakeKey(pass->set2Entries, set)
                                    : RDGDescriptorSetPool::Key::MakeLegacyKey(pass->rootSignature->GetInfo(), set),
                           pass->rootSignature).descriptor;
            bind_info.descriptor_sets[set] = descriptor;
            bind_info.pooled_descriptor_sets.push_back({descriptor, set});
            return descriptor;
        };

        // ——悬空槽自查器（2026-10-28悬空槽案产物，常驻哨兵勿删）——
        // set2声明槽 vs 实际写入元素对账（数组元素粒度）：Finish补齐门只查"条目有边"（一条即
        // done），元素粒度是VVL/校验器/GPU-AV全盲区（本案两现行犯=culling HiZ与ReSTIR深度的
        // 索引错位，噪声+设备死）。新pass/新shader回归时此处当场报缺失。纯日志零行为改动
        std::unordered_map<uint32_t, std::unordered_set<uint32_t>> set2Written;

        graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {

            if (edge->IsOutput()) return;    // 作为output声明时不需要view
            if (edge->set == NO_DESCRIPTOR_SET && !edge->asColor && !edge->asDepthStencil) return;

            RHITextureViewRef view = RDGTextureViewPool::Get(EngineContext::ThreadPool()->ThreadFrameIndex())->Allocate({
            .texture = texture->texture,    // Step1 已完成集中分配
            .format = texture->info.format,
            .viewType = edge->viewType,
            .subresource = edge->subresource }).textureView;
            bind_info.pooled_views.push_back(view);

            // rendering target：view与RHIRenderingInfo在主线程预构建，
            // 并行录制期worker不碰无锁的视图池；view随pooled_views由释放sweep统一归还
            if (edge->asColor || edge->asDepthStencil)
            {

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
                    .textureView = view,
                    .state = edge->state      // 声明布局与屏障同源（边的访问状态）：消除描述符/屏障双真值分叉
                };
                descriptor->UpdateDescriptor(updateInfo);
                if (edge->set == 2) set2Written[edge->binding].insert(edge->index);
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
                if (edge->set == 2) set2Written[edge->binding].insert(edge->index);
            }
        });

        // 悬空槽自查器·判定段：声明count(size)内每个未写元素报一次（进程内去重，双帧槽编译并发下
        // 最坏重复一条日志，无害）。healthy常见路径（全写/单元素）在首判早退零开销
        if (!pass->set2Entries.empty())
        {
            static std::unordered_set<std::string> reported;
            static const std::unordered_set<uint32_t> noIndices;
            for (const ShaderResourceEntry& e : pass->set2Entries)
            {
                const auto it = set2Written.find(e.binding);
                const std::unordered_set<uint32_t>& indices = it != set2Written.end() ? it->second : noIndices;
                if (indices.size() >= e.size) continue;
                for (uint32_t i = 0; i < e.size; i++)
                    if (!indices.count(i))
                    {
                        std::string key = pass->Name() + ":" + std::to_string(e.binding) + ":" + std::to_string(i);
                        if (reported.insert(std::move(key)).second)
                            ENGINE_LOG_WARN("[DanglingSlot] pass[{}] set2 b{}[{}]: 声明count={} 未写入（池化陈旧/悬空元素）",
                                pass->Name().c_str(), e.binding, i, e.size);
                    }
            }
        }
        // 静态采样器声明的set无条件领用（声明即绑定）：边的存在不再是领用条件——
        // NRD等constantBufferDataSize==0的dispatch不声明任何set1边，但shader仍访问set1采样器，
        // RS声明表本身就是"该set被使用"的证据（原pass->samplers旁路的隐式保证，勿再依赖边触发）。
        // 采样器值由CreateDescriptorSet/池Allocate播种写入，此处只保证set被领用
        if (pass->rootSignature != nullptr)
            for (const RHIStaticSampler& staticSampler : pass->rootSignature->GetInfo().GetStaticSamplers())
                ensure_descriptor_set(staticSampler.set);
    }
}
