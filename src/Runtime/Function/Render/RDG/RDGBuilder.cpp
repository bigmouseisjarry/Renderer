#include "RDGBuilder.h"

#include "Core/DependencyGraph/DependencyGraph.h"
#include "Core/Log/Log.h"
#include "Core/Util/StringFormat.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGEdge.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RDG/RDGNode.h"
#include "Function/Render/RDG/RDGPool.h"
#include "Function/Render/RHI/RHIStructs.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

RDGPassNodeRef RDGBlackBoard::Pass(std::string name)
{
    auto found = passes.find(name);
    if (found != passes.end()) {
        return found->second;
    }
    return nullptr;
}

RDGBufferNodeRef RDGBlackBoard::Buffer(std::string name)
{
    auto found = buffers.find(name);
    if (found != buffers.end()) {
        return found->second;
    }
    return nullptr;
}

RDGTextureNodeRef RDGBlackBoard::Texture(std::string name)
{
    auto found = textures.find(name);
    if (found != textures.end()) {
        return found->second;
    }
    return nullptr;
}

void RDGBlackBoard::AddPass(RDGPassNodeRef pass)
{
    passes[pass->Name()] = pass;
}

void RDGBlackBoard::AddBuffer(RDGBufferNodeRef buffer)
{
    buffers[buffer->Name()] = buffer;
}

void RDGBlackBoard::AddTexture(RDGTextureNodeRef texture)
{
    textures[texture->Name()] = texture;
}

RDGTextureBuilder RDGBuilder::CreateTexture(std::string name)
{
    RDGTextureNodeRef textureNode = graph->CreateTextureNode(name);
    blackBoard.AddTexture(textureNode);
    return RDGTextureBuilder(this, textureNode);
}

RDGBufferBuilder RDGBuilder::CreateBuffer(std::string name)
{
    RDGBufferNodeRef bufferNode = graph->CreateBufferNode(name);
    blackBoard.AddBuffer(bufferNode);
    return RDGBufferBuilder(this, bufferNode);
}

RDGTextureBuilder RDGBuilder::GetOrCreateTexture(std::string name)
{
    RDGTextureNodeRef textureNode = blackBoard.Texture(name);

    bool validationMode = true;
    if(textureNode == nullptr)
    {
        textureNode = graph->CreateTextureNode(name);
        blackBoard.AddTexture(textureNode);
        validationMode = false;
    }
    
    return RDGTextureBuilder(this, textureNode);
}

RDGBufferBuilder RDGBuilder::GetOrCreateBuffer(std::string name)
{   
    RDGBufferNodeRef bufferNode = blackBoard.Buffer(name);

    bool validationMode = true;
    if(bufferNode == nullptr)
    {
        bufferNode = graph->CreateBufferNode(name);
        blackBoard.AddBuffer(bufferNode);
        validationMode = false;
    }
       
    return RDGBufferBuilder(this, bufferNode, validationMode);
}

RDGRenderPassBuilder RDGBuilder::CreateRenderPass(std::string name)
{
    RDGRenderPassNodeRef passNode = graph->CreateRenderPassNode(name);
    blackBoard.AddPass(passNode);
    passes.push_back(passNode);
    return RDGRenderPassBuilder(this, passNode);
}

RDGComputePassBuilder RDGBuilder::CreateComputePass(std::string name)
{
    RDGComputePassNodeRef passNode = graph->CreateComputePassNode(name);
    blackBoard.AddPass(passNode);
    passes.push_back(passNode);
    return RDGComputePassBuilder(this, passNode);
}   

RDGRayTracingPassBuilder RDGBuilder::CreateRayTracingPass(std::string name)
{
    RDGRayTracingPassNodeRef passNode = graph->CreateRayTracingPassNode(name);
    blackBoard.AddPass(passNode);
    passes.push_back(passNode);
    return RDGRayTracingPassBuilder(this, passNode);
}   

RDGPresentPassBuilder RDGBuilder::CreatePresentPass(std::string name)
{
    RDGPresentPassNodeRef passNode = graph->CreatePresentPassNode(name);
    blackBoard.AddPass(passNode);
    passes.push_back(passNode);
    return RDGPresentPassBuilder(this, passNode);
}

RDGCopyPassBuilder RDGBuilder::CreateCopyPass(std::string name)
{
    RDGCopyPassNodeRef passNode = graph->CreateCopyPassNode(name);
    blackBoard.AddPass(passNode);
    passes.push_back(passNode);
    return RDGCopyPassBuilder(this, passNode);
}

RDGTextureHandle RDGBuilder::GetTexture(std::string name)
{
    auto node = blackBoard.Texture(name);
    if(node == nullptr) 
    {
        ENGINE_LOG_WARN("Unable to find RDG resource [{}], please check name!", name.c_str());
        return RDGTextureHandle(UINT64_MAX);
    }
    return node->GetHandle();
}

RDGBufferHandle RDGBuilder::GetBuffer(std::string name)
{
    auto node = blackBoard.Buffer(name);
    if(node == nullptr) 
    {
        ENGINE_LOG_WARN("Unable to find RDG resource [{}], please check name!", name.c_str());
        return RDGBufferHandle(UINT64_MAX);
    }
    return node->GetHandle();
}

void RDGBuilder::Execute()
{
    // TODO 还没做剔除
    for (auto& pass : passes) 
    {
        if(pass->isCulled || !pass) continue;
        // printf("rdg executing pass: %s\n", pass->Name().c_str());

        switch (pass->NodeType()) {
        case RDG_PASS_NODE_TYPE_RENDER:         ExecutePass(dynamic_cast<RDGRenderPassNodeRef>(pass));          break; 
        case RDG_PASS_NODE_TYPE_COMPUTE:        ExecutePass(dynamic_cast<RDGComputePassNodeRef>(pass));         break; 
        case RDG_PASS_NODE_TYPE_RAY_TRACING:    ExecutePass(dynamic_cast<RDGRayTracingPassNodeRef>(pass));      break; 
        case RDG_PASS_NODE_TYPE_PRESENT:        ExecutePass(dynamic_cast<RDGPresentPassNodeRef>(pass));         break; 
        case RDG_PASS_NODE_TYPE_COPY:           ExecutePass(dynamic_cast<RDGCopyPassNodeRef>(pass));            break; 
        default:                                ENGINE_LOG_FATAL("Unsupported RDG pass type!");
        }
    }

    for (auto& pass : passes)   // 释放池化资源
    {
        //ReleaseResource(pass);
        for(auto& descriptor : pass->pooledDescriptorSets)  // 池化的view在pass结束后就可以释放，但是描述符得全部执行完再释放？
        {
            RDGDescriptorSetPool::Get(EngineContext::ThreadPool()->ThreadFrameIndex())->Release({descriptor.first}, pass->rootSignature, descriptor.second);
        }
    }
}

void RDGBuilder::CreateInputBarriers(RDGPassNodeRef pass)
{
    graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture){

        if(edge->IsOutput()) return;
        RHIResourceState previousState = PreviousState(texture, pass, edge->subresource, false);
        //if(previousState != edge->state)  // 状态一样也加屏障？ 比如连续两个UAV读写的情况？
        {
            RHITextureBarrier barrier = {
                .texture = Resolve(texture),
                .srcState = previousState,
                .dstState = edge->state,
                .subresource = edge->subresource   
            };
            command->TextureBarrier(barrier);

            // printf("rdg resource %lld, raw: %s barrier: %d to %d\n", (int64_t)texture->texture.get(), ToHex((uint64_t)texture->texture->RawHandle(), false).c_str(), (uint32_t)previousState, (uint32_t)edge->state);
        }
    });

    graph->ForEachBuffer(pass, [&](RDGBufferEdgeRef edge, RDGBufferNodeRef buffer){
        
        if(edge->IsOutput()) return;
        RHIResourceState previousState = PreviousState(buffer, pass, false);
        //if(previousState != edge->state)  // 状态一样也加屏障？ 比如连续两个UAV读写的情况？
        {
            RHIBufferBarrier barrier = {
                .buffer = Resolve(buffer),
                .srcState = previousState,
                .dstState = edge->state,
                .offset = edge->offset,
                .size = edge->size
            };
            command->BufferBarrier(barrier);

            // printf("rdg resource %lld, raw: %s barrier: %d to %d\n", (int64_t)buffer->buffer.get(), ToHex((uint64_t)buffer->buffer->RawHandle(), false).c_str(), (uint32_t)previousState, (uint32_t)edge->state);
        }
    });
}

void RDGBuilder::CreateOutputBarriers(RDGPassNodeRef pass)
{
    graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture){

        if(!edge->IsOutput()) return;
        RHIResourceState previousState = PreviousState(texture, pass, edge->subresource, true);
        //if(previousState != edge->state)  // 状态一样也加屏障？ 比如连续两个UAV读写的情况？
        {
            RHITextureBarrier barrier = {
                .texture = Resolve(texture),
                .srcState = previousState,
                .dstState = edge->state,
                .subresource = edge->subresource   
            };
            command->TextureBarrier(barrier);

            // printf("rdg resource %lld, raw: %s barrier: %d to %d\n", (int64_t)texture->texture.get(), ToHex((uint64_t)texture->texture->RawHandle(), false).c_str(), (uint32_t)previousState, (uint32_t)edge->state);
        }
    });

    graph->ForEachBuffer(pass, [&](RDGBufferEdgeRef edge, RDGBufferNodeRef buffer){
        
        if(!edge->IsOutput()) return;
        RHIResourceState previousState = PreviousState(buffer, pass, true);
        //if(previousState != edge->state)  // 状态一样也加屏障？ 比如连续两个UAV读写的情况？
        {
            RHIBufferBarrier barrier = {
                .buffer = Resolve(buffer),
                .srcState = previousState,
                .dstState = edge->state,
                .offset = edge->offset,
                .size = edge->size
            };
            command->BufferBarrier(barrier);

            // printf("rdg resource %lld, raw: %s barrier: %d to %d\n", (int64_t)buffer->buffer.get(), ToHex((uint64_t)buffer->buffer->RawHandle(), false).c_str(), (uint32_t)previousState, (uint32_t)edge->state);
        }
    });
}

void RDGBuilder::PrepareDescriptorSet(RDGPassNodeRef pass)
{
    graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture){

        if (edge->IsOutput()) return;    // 作为output声明时不需要view
        if (edge->asColor || edge->asDepthStencil) return;    // render target/rendering target单独处理
        RHITextureViewRef view = RDGTextureViewPool::Get()->Allocate({
            .texture = Resolve(texture),
            .format = texture->info.format,
            .viewType = edge->viewType,
            .subresource = edge->subresource}).textureView;
        pass->pooledViews.push_back(view);

        if(pass->descriptorSets[edge->set] == nullptr && pass->rootSignature != nullptr)
        {
            auto descriptor = RDGDescriptorSetPool::Get(EngineContext::ThreadPool()->ThreadFrameIndex())->Allocate(pass->rootSignature, edge->set).descriptor;
            pass->descriptorSets[edge->set] = descriptor;
            pass->pooledDescriptorSets.push_back({descriptor, edge->set});
        }

        if((edge->asShaderRead || edge->asShaderReadWrite) && 
            pass->descriptorSets[edge->set] != nullptr)    
        {
            RHIDescriptorUpdateInfo updateInfo = {
                .binding = edge->binding,
                .index = edge->index,
                .resourceType = edge->type,
                .textureView = view
            };
            pass->descriptorSets[edge->set]->UpdateDescriptor(updateInfo);        
        }
    });

    graph->ForEachBuffer(pass, [&](RDGBufferEdgeRef edge, RDGBufferNodeRef buffer){

        if(pass->descriptorSets[edge->set] == nullptr && pass->rootSignature != nullptr)
        {
            auto descriptor = RDGDescriptorSetPool::Get(EngineContext::ThreadPool()->ThreadFrameIndex())->Allocate(pass->rootSignature, edge->set).descriptor;
            pass->descriptorSets[edge->set] = descriptor;
            pass->pooledDescriptorSets.push_back({descriptor, edge->set});
        }

        if((edge->asShaderRead || edge->asShaderReadWrite) && 
            pass->descriptorSets[edge->set] != nullptr)
        {
            RHIDescriptorUpdateInfo updateInfo = {
                .binding = edge->binding,
                .index = edge->index,
                .resourceType = edge->type,
                .buffer = Resolve(buffer),
                .bufferOffset = edge->offset,
                .bufferRange = edge->size
            };

            pass->descriptorSets[edge->set]->UpdateDescriptor(updateInfo); 
        }
    });

    for(auto& sampler : pass->samplers)
    {
        if(pass->descriptorSets[sampler.set] == nullptr && pass->rootSignature != nullptr)
        {
            auto descriptor = RDGDescriptorSetPool::Get(EngineContext::ThreadPool()->ThreadFrameIndex())->Allocate(pass->rootSignature, sampler.set).descriptor;
            pass->descriptorSets[sampler.set] = descriptor;
            pass->pooledDescriptorSets.push_back({descriptor, sampler.set});
        }

        if(pass->descriptorSets[sampler.set] != nullptr)
        {
            RHIDescriptorUpdateInfo updateInfo = {
                .binding = sampler.binding,
                .index = sampler.index,
                .resourceType = RESOURCE_TYPE_SAMPLER,
                .sampler = sampler.sampler
            };

            pass->descriptorSets[sampler.set]->UpdateDescriptor(updateInfo); 
        }
    }
}

//void RDGBuilder::PrepareRenderTarget(RDGRenderPassNodeRef pass, RHIRenderPassInfo& renderPassInfo)
//{
//    renderPassInfo.multiviewCount = pass->multiviewCount;
//    pass->ForEachTexture([&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture){
//
//        if(edge->IsOutput()) return;                            // 作为output声明时不需要view
//        if(!(edge->asColor || edge->asDepthStencil)) return;    // render target单独处理
//        RHITextureViewRef view = RDGTextureViewPool::Get()->Allocate({
//            .texture = Resolve(texture),
//            .format = texture->info.format,
//            .viewType = edge->viewType,
//            .subresource = edge->subresource}).textureView;
//        pass->pooledViews.push_back(view);
//
//        // 填充颜色附件
//        if(edge->asColor)
//        {
//            renderPassInfo.extent = {texture->info.extent.width, texture->info.extent.height};
//            renderPassInfo.layers = pass->multiviewCount > 0 ? 1 :                      // 启用multiview特性时，textureview的layer还是多个，framebuffer强制为1
//                edge->subresource.layerCount > 0 ? edge->subresource.layerCount : 1;
//
//            renderPassInfo.colorAttachments[edge->binding] = {
//                .textureView = view,
//                .loadOp = edge->loadOp,
//                .storeOp = edge->storeOp,
//                .clearColor = edge->clearColor,
//            };
//        }
//
//        // 填充深度/模板附件
//        else if (edge->asDepthStencil) 
//        {
//            renderPassInfo.extent = {texture->info.extent.width, texture->info.extent.height};
//            renderPassInfo.layers = pass->multiviewCount > 0 ? 1 :
//                edge->subresource.layerCount > 0 ? edge->subresource.layerCount : 1;
//
//            renderPassInfo.depthStencilAttachment = {
//                .textureView = view,
//                .loadOp = edge->loadOp,
//                .storeOp = edge->storeOp,
//                .clearDepth = edge->clearDepth,
//                .clearStencil = edge->clearStencil
//            };
//        }
//    });
//}

void RDGBuilder::PrepareRenderingTarget(RDGRenderPassNodeRef pass, RHIRenderingInfo& renderingInfo)
{
    renderingInfo.multiviewCount = pass->multiviewCount;

    graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {

        if (edge->IsOutput()) return;                            // 作为output声明时不需要view
        if (!(edge->asColor || edge->asDepthStencil)) return;    // rendering target单独处理
        RHITextureViewRef view = RDGTextureViewPool::Get()->Allocate({
            .texture = Resolve(texture),
            .format = texture->info.format,
            .viewType = edge->viewType,
            .subresource = edge->subresource }).textureView;
        pass->pooledViews.push_back(view);

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

void RDGBuilder::ReleaseResource(RDGPassNodeRef pass)
{

    graph->ForEachTexture(pass, [&](RDGTextureEdgeRef edge, RDGTextureNodeRef texture) {
        if (IsLastUsedPass(texture, pass, edge->IsOutput())) Release(texture, edge->state);
        });

    graph->ForEachBuffer(pass, [&](RDGBufferEdgeRef edge, RDGBufferNodeRef buffer) {
        if (IsLastUsedPass(buffer, pass, edge->IsOutput())) Release(buffer, edge->state);
        });

    for(auto& view : pass->pooledViews)
    {
        RDGTextureViewPool::Get()->Release({view});
    }
    pass->pooledViews.clear();
}

void RDGBuilder::ExecutePass(RDGRenderPassNodeRef pass)
{
    ENGINE_TIME_SCOPE_STR("RDGBuilder::ExecutePass::" + pass->Name());

    // 根据各个资源依赖的edge收集描述符更新信息以及framebuffer信息，
    // 调用Resolve()来分配和获取实际的RHI资源，资源将在最后一个使用的pass之后返回资源池
    // 处理状态转换的屏障

    PrepareDescriptorSet(pass);

    // 这里应该转成准备RenderingInfo
    //RHIRenderPassInfo renderPassInfo = {};
    //PrepareRenderTarget(pass, renderPassInfo);

	RHIRenderingInfo renderingInfo = {};
    PrepareRenderingTarget(pass, renderingInfo);

    // RHIRenderPassRef renderPass = EngineContext::RHI()->CreateRenderPass(renderPassInfo);   // renderPass和frameBuffer是在RHI层做的池化

    command->PushEvent(pass->Name(), {0.0f, 0.0f, 0.0f});

    CreateInputBarriers(pass);

    // GraphicsCommand->BeginRenderPass(renderPass);
	command->BeginRendering(renderingInfo);

    RDGPassContext context = {
        .command = command,
        .builder = this,
        .descriptors = pass->descriptorSets
    };
    context.passIndex[0] = pass->passIndex[0];
    context.passIndex[1] = pass->passIndex[1];
    context.passIndex[2] = pass->passIndex[2];
    pass->execute(context);

    // GraphicsCommand->EndRenderPass();
    command->EndRendering();

    CreateOutputBarriers(pass);

    ReleaseResource(pass);

    command->PopEvent();
}

void RDGBuilder::ExecutePass(RDGComputePassNodeRef pass)
{
    ENGINE_TIME_SCOPE_STR("RDGBuilder::ExecutePass::" + pass->Name());

    PrepareDescriptorSet(pass);

    command->PushEvent(pass->Name(), {1.0f, 0.0f, 0.0f});

    CreateInputBarriers(pass);

    RDGPassContext context = {
        .command = command,
        .builder = this,
        .descriptors = pass->descriptorSets
    };
    context.passIndex[0] = pass->passIndex[0];
    context.passIndex[1] = pass->passIndex[1];
    context.passIndex[2] = pass->passIndex[2];
    pass->execute(context);

    CreateOutputBarriers(pass);

    ReleaseResource(pass);

    command->PopEvent();
}

void RDGBuilder::ExecutePass(RDGRayTracingPassNodeRef pass)
{
    ENGINE_TIME_SCOPE_STR("RDGBuilder::ExecutePass::" + pass->Name());

    PrepareDescriptorSet(pass);

    command->PushEvent(pass->Name(), {0.0f, 1.0f, 0.0f});

    CreateInputBarriers(pass);

    RDGPassContext context = {
        .command = command,
        .builder = this,
        .descriptors = pass->descriptorSets
    };
    context.passIndex[0] = pass->passIndex[0];
    context.passIndex[1] = pass->passIndex[1];
    context.passIndex[2] = pass->passIndex[2];
    pass->execute(context);

    CreateOutputBarriers(pass);

    ReleaseResource(pass);

    command->PopEvent();
}

void RDGBuilder::ExecutePass(RDGPresentPassNodeRef pass)
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
            Resolve(texture)->GetDefaultSubresourceLayers() : edges[1].edge->subresourceLayer;
    }
    else
    {
        presentTexture = edges[1].node;
        texture = edges[0].node;
        subresource = edges[0].edge->subresource.aspect == TEXTURE_ASPECT_NONE ?
            Resolve(texture)->GetDefaultSubresourceLayers() : edges[0].edge->subresourceLayer;
    }

    command->PushEvent(pass->Name(), {0.0f, 0.0f, 1.0f});

    CreateInputBarriers(pass);

    command->TextureBarrier({Resolve(presentTexture), RESOURCE_STATE_PRESENT, RESOURCE_STATE_TRANSFER_DST});
    command->CopyTexture(   Resolve(texture), subresource, 
                            Resolve(presentTexture), {TEXTURE_ASPECT_COLOR, 0, 0, 1});
    command->TextureBarrier({Resolve(presentTexture), RESOURCE_STATE_TRANSFER_DST, RESOURCE_STATE_PRESENT});

    CreateOutputBarriers(pass);

    ReleaseResource(pass);

    command->PopEvent();
}

void RDGBuilder::ExecutePass(RDGCopyPassNodeRef pass)
{
    RDGBufferNodeRef bufferFrom = nullptr;
    RDGBufferNodeRef bufferTo = nullptr;
    uint32_t offsetFrom;
    uint32_t offsetTo;
    uint32_t size;

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

    command->PushEvent(pass->Name(), {1.0f, 1.0f, 0.0f});

    CreateInputBarriers(pass);

    if(bufferFrom != nullptr && bufferTo != nullptr)
    {
        command->CopyBuffer(Resolve(bufferFrom), offsetFrom, 
                            Resolve(bufferTo), offsetTo, size);
    }

    if(textureFrom != nullptr && textureTo != nullptr)
    {
        command->CopyTexture(   Resolve(textureFrom), fromSubresource, 
                                Resolve(textureTo), toSubresource);

        if(pass->generateMip) 
        {
            RHITextureBarrier barrier = {
                .texture = Resolve(textureTo),
                .srcState = RESOURCE_STATE_TRANSFER_DST,
                .dstState = RESOURCE_STATE_TRANSFER_SRC,
                .subresource = {} 
            };
            command->TextureBarrier(barrier);
            command->GenerateMips(Resolve(textureTo)); // 默认纹理处于src状态，需要手动加屏障

            barrier = {
                .texture = Resolve(textureTo),
                .srcState = RESOURCE_STATE_TRANSFER_SRC,
                .dstState = RESOURCE_STATE_TRANSFER_DST,
                .subresource = {} 
            };
            command->TextureBarrier(barrier);
        }
    }

    CreateOutputBarriers(pass);

    ReleaseResource(pass);

    command->PopEvent();
}

RHITextureRef RDGBuilder::Resolve(RDGTextureNodeRef textureNode)
{   
    if(textureNode->texture == nullptr)
    {
        auto pooledTexture = RDGTexturePool::Get()->Allocate(textureNode->info);
        textureNode->texture = pooledTexture.texture;
        textureNode->initState = pooledTexture.state;

        // printf("rdg resource %s allocated: %lld, raw: %s\n", textureNode->Name().c_str(), (int64_t)textureNode->texture.get(), ToHex((uint64_t)textureNode->texture->RawHandle(), false).c_str());
    }

    // // printf("rdg resource %s raw: %s\n", textureNode->Name().c_str(), ToHex((uint64_t)textureNode->texture->RawHandle(), false).c_str());


    return textureNode->texture;
}

RHIBufferRef RDGBuilder::Resolve(RDGBufferNodeRef bufferNode)
{
    if(bufferNode->buffer == nullptr)
    {
        auto pooledBuffer = RDGBufferPool::Get()->Allocate(bufferNode->info);
        bufferNode->buffer = pooledBuffer.buffer;
        bufferNode->initState = pooledBuffer.state;

        // printf("rdg resource %s allocated: %lld, raw: %s\n", bufferNode->Name().c_str(), (int64_t)bufferNode->buffer.get(), ToHex((uint64_t)bufferNode->buffer->RawHandle(), false).c_str());
    }

    // // printf("rdg resource %s raw: %s\n", bufferNode->Name().c_str(), ToHex((uint64_t)bufferNode->buffer->RawHandle(), false).c_str());

    return bufferNode->buffer;
}   

void RDGBuilder::Release(RDGTextureNodeRef textureNode, RHIResourceState state)
{
    if(textureNode->IsImported()) return;
    if(textureNode->texture) 
    {
        // printf("rdg resource %s released: %lld, raw: %s\n", textureNode->Name().c_str(), (int64_t)textureNode->texture.get(), ToHex((uint64_t)textureNode->texture->RawHandle(), false).c_str());

        RDGTexturePool::Get()->Release({ textureNode->texture, state});
        textureNode->texture = nullptr;
        textureNode->initState = RESOURCE_STATE_UNDEFINED;
    }

}

void RDGBuilder::Release(RDGBufferNodeRef bufferNode, RHIResourceState state)
{
    if(bufferNode->IsImported()) return;
    if(bufferNode->buffer) 
    {
        // printf("rdg resource %s released: %lld, raw: %s\n", bufferNode->Name().c_str(), (int64_t)bufferNode->buffer.get(), ToHex((uint64_t)bufferNode->buffer->RawHandle(), false).c_str());

        RDGBufferPool::Get()->Release({ bufferNode->buffer, state});
        bufferNode->buffer = nullptr;
        bufferNode->initState = RESOURCE_STATE_UNDEFINED;
    }
}

RHIResourceState RDGBuilder::PreviousState(RDGTextureNodeRef textureNode, RDGPassNodeRef passNode, TextureSubresourceRange subresource, bool output)
{
    Resolve(textureNode);
    DAGID currentID = passNode->ID();
    DAGID previousID = UINT64_MAX;

    RHIResourceState previousState = textureNode->initState;        // 若没有前序引用，那状态就是资源本身的初始状态

    graph->ForEachPass(textureNode, [&](RDGTextureEdgeRef edge, RDGPassNodeRef pass){

        bool isOutputFirst = output ? !edge->IsOutput() : edge->IsOutput();
        bool isPrevoiusPass = output ? pass->ID() <= currentID : pass->ID() < currentID;
        bool isSubresourceCovered = subresource.IsDefault() ||                      // 无状态地追踪整个子资源状态实在有些困难，现在支持的方法是：
                                    edge->subresource.IsDefault() ||                // 1. 若目标状态是默认范围，那只追踪前序最近的状态
                                    subresource == edge->subresource;               // 2. 若目标状态是子范围，那必须追踪前序最近的完全一致的子范围/默认范围的状态
                                                                                    // 再合理利用output的手动屏障，应该能够完成全部子范围的管理
        if(!(isPrevoiusPass && isSubresourceCovered)) return;
        if(pass->ID() > previousID || previousID == UINT64_MAX)     // 不同的前序pass，取最后一个的状态
        {
            previousState = edge->state;
            previousID = pass->ID();
        }
        else if(pass->ID() == previousID &&                         // 同一个前序pass，考虑取输入or输出状态
                isOutputFirst)           
        {    
            previousState = edge->state;
            previousID = pass->ID();
        }   
    });

    return previousState;
}   

RHIResourceState RDGBuilder::PreviousState(RDGBufferNodeRef bufferNode, RDGPassNodeRef passNode, uint32_t offset, uint32_t size, bool output)
{
    Resolve(bufferNode);
    DAGID currentID = passNode->ID();
    DAGID previousID = UINT64_MAX;

    RHIResourceState previousState = bufferNode->initState;         // 若没有前序引用，那状态就是资源本身的初始状态

    graph->ForEachPass(bufferNode, [&](RDGBufferEdgeRef edge, RDGPassNodeRef pass){

        bool isOutputFirst = output ? !edge->IsOutput() : edge->IsOutput();
        bool isPrevoiusPass = output ? pass->ID() <= currentID : pass->ID() < currentID;
        bool isSubresourceCovered = (offset == 0 && size == 0) || 
                                    (edge->offset == 0 && edge->size == 0) || 
                                    (offset == edge->offset && size == edge->size);  // 同texture里的策略
        
        if(!(isPrevoiusPass && isSubresourceCovered)) return;
        if(pass->ID() > previousID || previousID == UINT64_MAX)     // 不同的前序pass，取最后一个的状态
        {
            previousState = edge->state;
            previousID = pass->ID();
        }
        else if(pass->ID() == previousID &&                         // 同一个前序pass，考虑取输入or输出状态
                isOutputFirst)           
        {    
            previousState = edge->state;
            previousID = pass->ID();
        }   
    });

    return previousState;
}

bool RDGBuilder::IsLastUsedPass(RDGTextureNodeRef textureNode, RDGPassNodeRef passNode, bool output)
{
    DAGID currentID = passNode->ID();
    bool last = true;

    graph->ForEachPass(textureNode, [&](RDGTextureEdgeRef edge, RDGPassNodeRef pass){
        if(pass->ID() > currentID) last = false;
        if(!output && pass->ID() == currentID && edge->IsOutput()) last = false;
    });

    return last;
}

bool RDGBuilder::IsLastUsedPass(RDGBufferNodeRef bufferNode, RDGPassNodeRef passNode, bool output)
{
    DAGID currentID = passNode->ID();
    bool last = true;

    graph->ForEachPass(bufferNode, [&](RDGBufferEdgeRef edge, RDGPassNodeRef pass){
        if(pass->ID() > currentID) last = false;
        if(!output && pass->ID() == currentID && edge->IsOutput()) last = false;
    });

    return last;
}

RDGTextureBuilder& RDGTextureBuilder::Import(RHITextureRef texture, RHIResourceState initState)
{
    if(validationMode)
    {
        assert(this->texture->isImported == true);
        assert(this->texture->texture == texture);
        assert(this->texture->initState == initState);
    }
    else
    {
        this->texture->isImported = true;
        this->texture->texture = texture;
        this->texture->info = texture->GetInfo();
        this->texture->initState = initState;
    }

    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::Exetent(Extent3D extent)
{
    if(validationMode)
    {
        assert(texture->info.extent == extent);
    }
    else
    {
        texture->info.extent = extent;
    }

    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::Format(RHIFormat format)
{
    if(validationMode)
    {
        assert(texture->info.format == format);
    }
    else
    {
        texture->info.format = format;
    }

    return *this;
}

RDGTextureBuilder& RDGTextureBuilder::MemoryUsage(enum MemoryUsage memoryUsage)
{
    if(validationMode)
    {
        assert(texture->info.memoryUsage == memoryUsage);
    }
    else
    {
        texture->info.memoryUsage = memoryUsage;
    }
    
    return  *this;
}

RDGTextureBuilder& RDGTextureBuilder::AllowReadWrite()
{
    if(validationMode)
    {
        assert(texture->info.type & RESOURCE_TYPE_RW_TEXTURE);
    }
    else
    {
        texture->info.type |= RESOURCE_TYPE_RW_TEXTURE;
    }

    return  *this;
}

RDGTextureBuilder& RDGTextureBuilder::AllowRenderTarget()
{
    if(validationMode)
    {
        assert(texture->info.type & RESOURCE_TYPE_RENDER_TARGET);
    }
    else
    {
        texture->info.type |= RESOURCE_TYPE_RENDER_TARGET;
    }

    return  *this;
}

RDGTextureBuilder& RDGTextureBuilder::AllowDepthStencil()
{
    if(validationMode)
    {
        assert(texture->info.type & RESOURCE_TYPE_RENDER_TARGET);
    }
    else
    {
        texture->info.type |= RESOURCE_TYPE_RENDER_TARGET;
    }

    return  *this;
}

RDGTextureBuilder& RDGTextureBuilder::MipLevels(uint32_t mipLevels)
{
    if(validationMode)
    {
        assert(texture->info.mipLevels = mipLevels);
    }
    else
    {
        texture->info.mipLevels = mipLevels;
    }

    return *this;
}   

RDGTextureBuilder& RDGTextureBuilder::ArrayLayers(uint32_t arrayLayers)
{
    if(validationMode)
    {
        assert(texture->info.arrayLayers == arrayLayers);
    }
    else
    {
        texture->info.arrayLayers = arrayLayers;
    }
    
    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::Import(RHIBufferRef buffer, RHIResourceState initState)
{
    if(validationMode)
    {
        assert(this->buffer->isImported == true);
        assert(this->buffer->buffer == buffer);
        assert(this->buffer->initState == initState);
    }
    else 
    {
        this->buffer->isImported = true;
        this->buffer->buffer = buffer;
        this->buffer->info = buffer->GetInfo();
        this->buffer->initState = initState;
    }

    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::Size(uint32_t size)
{
    if(validationMode)
    {
        assert(buffer->info.size == size);
    }
    else
    {
        buffer->info.size = size;
    }

    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::MemoryUsage(enum MemoryUsage memoryUsage)
{
    if(validationMode)
    {
        assert(buffer->info.memoryUsage == memoryUsage);
    }
    else
    {
        buffer->info.memoryUsage = memoryUsage;
    }

    return  *this;
}

RDGBufferBuilder& RDGBufferBuilder::AllowVertexBuffer()
{
    if(validationMode)
    {
        assert(buffer->info.type & RESOURCE_TYPE_VERTEX_BUFFER);
    }
    else
    {
        buffer->info.type |= RESOURCE_TYPE_VERTEX_BUFFER;
    }

    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::AllowIndexBuffer()
{
    if(validationMode)
    {
        assert(buffer->info.type & RESOURCE_TYPE_INDEX_BUFFER);
    }
    else
    {
        buffer->info.type |= RESOURCE_TYPE_INDEX_BUFFER;
    }

    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::AllowReadWrite()
{
    if(validationMode)
    {
        assert(buffer->info.type & RESOURCE_TYPE_RW_BUFFER);
    }
    else
    {
        buffer->info.type |= RESOURCE_TYPE_RW_BUFFER;
    }

    return *this;
}

RDGBufferBuilder& RDGBufferBuilder::AllowRead()
{
    if(validationMode)
    {
        assert(buffer->info.type & RESOURCE_TYPE_UNIFORM_BUFFER);
    }
    else
    {
        buffer->info.type |= RESOURCE_TYPE_UNIFORM_BUFFER;
    }

    return *this;    
}

RDGRenderPassBuilder& RDGRenderPassBuilder::PassIndex(uint32_t x, uint32_t y, uint32_t z)
{
    pass->passIndex[0] = x;
    pass->passIndex[1] = y;
    pass->passIndex[2] = z;
    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::RootSignature(RHIRootSignatureRef rootSignature)
{
    pass->rootSignature = rootSignature;
    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::DescriptorSet(uint32_t set, RHIDescriptorSetRef descriptorSet)
{
    pass->descriptorSets[set] = descriptorSet;
    return *this;  
}

RDGRenderPassBuilder& RDGRenderPassBuilder::Sampler(uint32_t set, uint32_t binding, uint32_t index, RHISamplerRef sampler)
{
    pass->samplers.push_back({
        .sampler = sampler,
        .set = set,
        .binding = binding,
        .index = index
    });
    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_UNIFORM_BUFFER;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_TEXTURE;
    edge->viewType = viewType;

    graph->Link(graph->GetTextureNode(texture.ID()), pass, edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderReadWrite = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_RW_BUFFER;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->asShaderReadWrite = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_RW_TEXTURE;
    edge->viewType = viewType;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::Color(  uint32_t binding, RDGTextureHandle texture, 
                                                    AttachmentLoadOp load, 
                                                    AttachmentStoreOp store, 
                                                    Color4 clearColor, 
                                                    TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_COLOR_ATTACHMENT;
    edge->loadOp = load;
    edge->storeOp = store;
    edge->clearColor = clearColor;
    edge->subresource = subresource;
    edge->asColor = true;
    edge->binding = binding;
    edge->viewType = subresource.layerCount > 1 ? VIEW_TYPE_2D_ARRAY : VIEW_TYPE_2D;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}   

RDGRenderPassBuilder& RDGRenderPassBuilder::DepthStencil(   RDGTextureHandle texture, 
                                                            AttachmentLoadOp load, 
                                                            AttachmentStoreOp store, 
                                                            float clearDepth,
                                                            uint32_t clearStencil,
                                                            TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_DEPTH_STENCIL_ATTACHMENT;
    edge->loadOp = load;
    edge->storeOp = store;
    edge->clearDepth = clearDepth;
    edge->clearStencil = clearStencil;
    edge->subresource = subresource;
    edge->asDepthStencil = true;
    edge->viewType = subresource.layerCount > 1 ? VIEW_TYPE_2D_ARRAY : VIEW_TYPE_2D;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::Multiview(uint32_t multiviewCount)
{
    pass->multiviewCount = multiviewCount;

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::OutputRead(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputRead = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::OutputRead(RDGTextureHandle texture, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::OutputReadWrite(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::OutputReadWrite(RDGTextureHandle texture, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::Execute(const RDGPassExecuteFunc& execute)
{
    pass->execute = execute;
    return *this;
}

RDGRenderPassBuilder& RDGRenderPassBuilder::AddFlag(const RDGPassFlags flag)
{
    pass->add_flags(flag);
    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::PassIndex(uint32_t x, uint32_t y, uint32_t z)
{
    pass->passIndex[0] = x;
    pass->passIndex[1] = y;
    pass->passIndex[2] = z;
    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::RootSignature(RHIRootSignatureRef rootSignature)
{
    pass->rootSignature = rootSignature;
    return *this;
}   

RDGComputePassBuilder& RDGComputePassBuilder::DescriptorSet(uint32_t set, RHIDescriptorSetRef descriptorSet)
{
    pass->descriptorSets[set] = descriptorSet;
    return *this;  
}  

RDGComputePassBuilder& RDGComputePassBuilder::Sampler(uint32_t set, uint32_t binding, uint32_t index, RHISamplerRef sampler)
{
    pass->samplers.push_back({
        .sampler = sampler,
        .set = set,
        .binding = binding,
        .index = index
    });
    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_UNIFORM_BUFFER;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_TEXTURE;
    edge->viewType = viewType;

    graph->Link(graph->GetTextureNode(texture.ID()), pass, edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderReadWrite = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_RW_BUFFER;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->asShaderReadWrite = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_RW_TEXTURE;
    edge->viewType = viewType;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::OutputRead(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputRead = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::OutputRead(RDGTextureHandle texture, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::OutputReadWrite(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::OutputReadWrite(RDGTextureHandle texture, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::OutputIndirectDraw(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_INDIRECT_ARGUMENT;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputIndirectDraw = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::AddFlag(const RDGPassFlags flag)
{
    pass->add_flags(flag);
    return *this;
}

RDGComputePassBuilder& RDGComputePassBuilder::Execute(const RDGPassExecuteFunc& execute)
{
    pass->execute = execute;
    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::PassIndex(uint32_t x, uint32_t y, uint32_t z)
{
    pass->passIndex[0] = x;
    pass->passIndex[1] = y;
    pass->passIndex[2] = z;
    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::RootSignature(RHIRootSignatureRef rootSignature)
{
    pass->rootSignature = rootSignature;
    return *this;
}   

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::DescriptorSet(uint32_t set, RHIDescriptorSetRef descriptorSet)
{
    pass->descriptorSets[set] = descriptorSet;
    return *this;  
}  

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::Sampler(uint32_t set, uint32_t binding, uint32_t index, RHISamplerRef sampler)
{
    pass->samplers.push_back({
        .sampler = sampler,
        .set = set,
        .binding = binding,
        .index = index
    });
    return *this; 
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_UNIFORM_BUFFER;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::Read(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->asShaderRead = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_TEXTURE;
    edge->viewType = viewType;

    graph->Link(graph->GetTextureNode(texture.ID()), pass, edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asShaderReadWrite = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_RW_BUFFER;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::ReadWrite(uint32_t set, uint32_t binding, uint32_t index, RDGTextureHandle texture, TextureViewType viewType, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->asShaderReadWrite = true;
    edge->set = set;
    edge->binding = binding;
    edge->index = index;
    edge->type = RESOURCE_TYPE_RW_TEXTURE;
    edge->viewType = viewType;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::OutputRead(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputRead = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::OutputRead(RDGTextureHandle texture, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->subresource = subresource;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::OutputReadWrite(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::OutputReadWrite(RDGTextureHandle texture, TextureSubresourceRange subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresource = subresource;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::AddFlag(const RDGPassFlags flag)
{
    pass->add_flags(flag);
    return *this;
}

RDGRayTracingPassBuilder& RDGRayTracingPassBuilder::Execute(const RDGPassExecuteFunc& execute)
{
    pass->execute = execute;
    return *this;
}

RDGPresentPassBuilder& RDGPresentPassBuilder::Texture(RDGTextureHandle texture, TextureSubresourceLayers subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_TRANSFER_SRC;
    edge->subresourceLayer = subresource;

    graph->Link(graph->GetTextureNode(texture.ID()), pass, edge);

    return *this;
}

RDGPresentPassBuilder& RDGPresentPassBuilder::PresentTexture(RDGTextureHandle texture)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_PRESENT;
    edge->asPresent = true;

    graph->Link(graph->GetTextureNode(texture.ID()), pass, edge);

    return *this;    
}

RDGCopyPassBuilder& RDGCopyPassBuilder::From(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_TRANSFER_SRC;
    edge->offset = offset;
    edge->size = size;
    edge->asTransferSrc = true;

    graph->Link(graph->GetBufferNode(buffer.ID()), pass, edge);

    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::From(RDGTextureHandle texture, TextureSubresourceLayers subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_TRANSFER_SRC;
    edge->subresourceLayer = subresource;
    edge->asTransferSrc = true;

    graph->Link(graph->GetTextureNode(texture.ID()), pass, edge);

    return *this;    
}

RDGCopyPassBuilder& RDGCopyPassBuilder::To(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_TRANSFER_DST;
    edge->offset = offset;
    edge->size = size;
    edge->asTransferDst = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::To(RDGTextureHandle texture, TextureSubresourceLayers subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_TRANSFER_DST;
    edge->subresourceLayer = subresource;
    edge->asTransferDst = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this;    
}

RDGCopyPassBuilder& RDGCopyPassBuilder::GenerateMips()
{
    pass->generateMip = true;
    return *this;   
}

RDGCopyPassBuilder& RDGCopyPassBuilder::OutputRead(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_SHADER_RESOURCE;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputRead = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::OutputRead(RDGTextureHandle texture, TextureSubresourceLayers subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresourceLayer = subresource;
    edge->asOutputRead = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this; 
}

RDGCopyPassBuilder& RDGCopyPassBuilder::OutputReadWrite(RDGBufferHandle buffer, uint32_t offset, uint32_t size)
{
    RDGBufferEdgeRef edge = new RDGBufferEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->offset = offset;
    edge->size = size;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetBufferNode(buffer.ID()), edge);

    return *this;
}

RDGCopyPassBuilder& RDGCopyPassBuilder::OutputReadWrite(RDGTextureHandle texture, TextureSubresourceLayers subresource)
{
    RDGTextureEdgeRef edge = new RDGTextureEdge();
    edge->state = RESOURCE_STATE_UNORDERED_ACCESS;
    edge->subresourceLayer = subresource;
    edge->asOutputReadWrite = true;

    graph->Link(pass, graph->GetTextureNode(texture.ID()), edge);

    return *this; 
}

RDGCopyPassBuilder& RDGCopyPassBuilder::AddFlag(const RDGPassFlags flag)
{
    pass->add_flags(flag);
    return *this;
}


