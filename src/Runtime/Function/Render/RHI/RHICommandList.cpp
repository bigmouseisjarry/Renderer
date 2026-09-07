#include "RHICommandList.h"
#include "RHI.h"
#include "RHIResource.h"

#include <cstdint>
#include <cstdio>

RHICommandList::~RHICommandList() 
{ 
    info.pool->ReturnToPool(info.context); 
    info.pool = nullptr;
    info.context = nullptr;
}

void* RHICommandList::RawHandle()
{
    return info.context->RawHandle();
}

void RHICommandList::BeginCommand()
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->BeginCommand();
    else ADD_COMMAND(BeginCommand);
}

void RHICommandList::EndCommand()
{
    COMMANDLIST_DEBUG_OUTPUT();
    COMMANDLIST_DEBUG_RESET_INDEX();
    if(info.byPass) info.context->EndCommand();
    else ADD_COMMAND(EndCommand);
}

RHIQueueRef RHICommandList::GetQueue() const
{
    return info.pool->GetQueue();
}

void RHICommandList::ReplayList(RHICommandList* list)
{
    if (!list->info.byPass)     // 延迟模式：先回放各自队列到自己的context；byPass列表队列已空
    {
        // LOG_DEBUG("Recording GraphicsCommand list in delay mode.");
        for (int32_t i = 0; i < list->commands.size(); i++)
        {
            list->commands[i]->Execute(list->info.context);
            delete list->commands[i];
        }
        list->commands.clear();
    }
}

void RHICommandList::Submit(const RHIQueueSubmitBatch& batch)
{
    assert(batch.queue != nullptr);

    // 回放延迟模式的列表到各自的context（byPass列表命令队列已空，直接收集）
    std::vector<RHICommandContextRef> contexts;
    contexts.reserve(batch.commandLists.size());
    for (const RHICommandListRef& list : batch.commandLists)
    {
        if (list == nullptr) continue;

        ReplayList(list.get());

        // 批次内列表的归属队列族必须与目标队列一致（原注释级约定升级为断言：
        // 命令缓冲只能在分配它的队列族上提交执行）
        assert(list->GetQueue()->GetFamilyIndex() == batch.queue->GetFamilyIndex());

        contexts.push_back(list->info.context);
    }

    // 经载体context发出（空contexts=空提交：信号量中继/join收口批，合法）
    info.context->Submit(batch, contexts);
}

void RHICommandList::Execute(RHIFenceRef signalFence, RHISemaphoreRef waitSemaphore, RHISemaphoreRef signalSemaphore)
{
    ReplayList(this);

    // 单列表批次
    RHIQueueSubmitBatch batch;
    batch.queue = GetQueue();
    batch.signalFence = signalFence;
    if (waitSemaphore != nullptr)
        batch.waits.push_back({waitSemaphore, 0, false, RESOURCE_STATE_UNDEFINED});   // UNDEFINED=宽掩码约定（如swapchain acquire）
    if (signalSemaphore != nullptr)
        batch.signals.push_back({signalSemaphore, 0, false});

    std::vector<RHICommandContextRef> contexts = {info.context};
    info.context->Submit(batch, contexts);
}

void RHICommandList::ExecuteBatch(const std::vector<RHICommandListRef>& lists, RHIFenceRef signalFence, RHISemaphoreRef waitSemaphore, RHISemaphoreRef signalSemaphore)
{
    if (lists.empty()) return;

    RHIQueueSubmitBatch batch;
    batch.queue = lists.front()->GetQueue();
    batch.commandLists = lists;
    batch.signalFence = signalFence;
    if (waitSemaphore != nullptr)
        batch.waits.push_back({waitSemaphore, 0, false, RESOURCE_STATE_UNDEFINED});
    if (signalSemaphore != nullptr)
        batch.signals.push_back({signalSemaphore, 0, false});

    lists.front()->Submit(batch);   // 载体=首列表（同队列断言在Submit内）
}

void RHICommandList::ExecuteQueueSubmitPlan(const RHIQueueSubmitPlan& plan)
{
    // 载体=this：各批次目标队列由批次显式携带，与载体归属无关；
    // 空commandLists批次（信号量中继/join收口）亦经载体context发出
    for (const RHIQueueSubmitBatch& batch : plan.batches)
    {
        if (batch.queue == nullptr) continue;

        Submit(batch);
    }
}

void RHICommandList::TextureBarrier(const RHITextureBarrier& barrier)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->TextureBarrier(barrier);
    else ADD_COMMAND(TextureBarrier, barrier);
}

void RHICommandList::BufferBarrier(const RHIBufferBarrier& barrier)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->BufferBarrier(barrier);
    else ADD_COMMAND(BufferBarrier, barrier);
}

void RHICommandList::CopyTextureToBuffer(RHITextureRef src, TextureSubresourceLayers srcSubresource, RHIBufferRef dst, uint64_t dstOffset)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->CopyTextureToBuffer(src, srcSubresource, dst, dstOffset);
    else ADD_COMMAND(CopyTextureToBuffer, src, srcSubresource, dst, dstOffset);
}

void RHICommandList::CopyBufferToTexture(RHIBufferRef src, uint64_t srcOffset, RHITextureRef dst, TextureSubresourceLayers dstSubresource)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->CopyBufferToTexture(src, srcOffset, dst, dstSubresource);
    else ADD_COMMAND(CopyBufferToTexture, src, srcOffset, dst, dstSubresource);
}

void RHICommandList::CopyBuffer(RHIBufferRef src, uint64_t srcOffset, RHIBufferRef dst, uint64_t dstOffset, uint64_t size)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->CopyBuffer(src, srcOffset, dst, dstOffset, size);
    else ADD_COMMAND(CopyBuffer, src, srcOffset, dst, dstOffset, size);
}

void RHICommandList::CopyTexture(RHITextureRef src, TextureSubresourceLayers srcSubresource, RHITextureRef dst, TextureSubresourceLayers dstSubresource)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->CopyTexture(src, srcSubresource, dst, dstSubresource);
    else ADD_COMMAND(CopyTexture, src, srcSubresource, dst, dstSubresource);
}

void RHICommandList::GenerateMips(RHITextureRef src)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->GenerateMips(src);
    else ADD_COMMAND(GenerateMips, src);
}

void RHICommandList::BuildTopLevelAccelerationStructure(RHITopLevelAccelerationStructureRef tlas)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->BuildTopLevelAccelerationStructure(tlas);
    else ADD_COMMAND(BuildTLAS, tlas);
}

void RHICommandList::PushEvent(const std::string& name, Color3 color) 
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->PushEvent(name, color);
    else ADD_COMMAND(PushEvent, name, color);
}

void RHICommandList::PopEvent() 
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->PopEvent();
    else ADD_COMMAND(PopEvent);
}

void RHICommandList::BeginRendering(const RHIRenderingInfo& rendering)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if (info.byPass)info.context->BeginRendering(rendering);
    else ADD_COMMAND(BeginRendering, rendering);
}

void RHICommandList::EndRendering()
{
    COMMANDLIST_DEBUG_OUTPUT();
    if (info.byPass) info.context->EndRendering();
    else ADD_COMMAND(EndRendering);
}

void RHICommandList::SetViewport(Offset2D min, Offset2D max)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->SetViewport(min, max);
    else ADD_COMMAND(SetViewport, min, max);
}

void RHICommandList::SetScissor(Offset2D min, Offset2D max) 
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->SetScissor(min, max);
    else ADD_COMMAND(SetScissor, min, max);
}

void RHICommandList::ClearScissors(const std::vector<ClearAttachment>& attachments, const std::vector<Rect2D>& scissors, uint32_t baseArrayLayer, uint32_t layerCount)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->ClearScissors(attachments, scissors, baseArrayLayer, layerCount);
    else ADD_COMMAND(ClearScissors, attachments, scissors, baseArrayLayer, layerCount);
}

void RHICommandList::SetDepthBias(float constantBias, float slopeBias, float clampBias)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->SetDepthBias(constantBias, slopeBias, clampBias);
    else ADD_COMMAND(SetDepthBias, constantBias, slopeBias, clampBias);
}

void RHICommandList::SetLineWidth(float width)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->SetLineWidth(width);
    else ADD_COMMAND(SetLineWidth, width);
}

void RHICommandList::SetGraphicsPipeline(RHIGraphicsPipelineRef graphicsPipeline) 
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->SetGraphicsPipeline(graphicsPipeline); 
    else ADD_COMMAND(SetGraphicsPipeline, graphicsPipeline);
}

void RHICommandList::SetComputePipeline(RHIComputePipelineRef computePipeline) 
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->SetComputePipeline(computePipeline); 
    else ADD_COMMAND(SetComputePipeline, computePipeline);
}	

void RHICommandList::SetRayTracingPipeline(RHIRayTracingPipelineRef rayTracingPipeline)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->SetRayTracingPipeline(rayTracingPipeline); 
    else ADD_COMMAND(SetRayTracingPipeline, rayTracingPipeline);
}

void RHICommandList::PushConstants(void* data, uint16_t size, ShaderFrequency frequency)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->PushConstants(data, size, frequency);
    else ADD_COMMAND(PushConstants, data, size, frequency);
}

void RHICommandList::BindDescriptorSet(RHIDescriptorSetRef descriptor, uint32_t set)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->BindDescriptorSet(descriptor, set);
    else ADD_COMMAND(BindDescriptorSet, descriptor, set);
}

void RHICommandList::BindVertexBuffer(RHIBufferRef vertexBuffer, uint32_t streamIndex, uint32_t offset)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->BindVertexBuffer(vertexBuffer, streamIndex, offset);
    else ADD_COMMAND(BindVertexBuffer, vertexBuffer, streamIndex, offset);
}

void RHICommandList::BindIndexBuffer(RHIBufferRef indexBuffer, uint32_t offset)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->BindIndexBuffer(indexBuffer, offset);
    else ADD_COMMAND(BindIndexBuffer, indexBuffer, offset);
}

void RHICommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->Dispatch(groupCountX, groupCountY, groupCountZ);
    else ADD_COMMAND(Dispatch, groupCountX, groupCountY, groupCountZ);
}

void RHICommandList::DispatchIndirect(RHIBufferRef argumentBuffer, uint32_t argumentOffset) 
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->DispatchIndirect(argumentBuffer, argumentOffset);
    else ADD_COMMAND(DispatchIndirect, argumentBuffer, argumentOffset);
}

void RHICommandList::TraceRays(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) 
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->TraceRays(groupCountX, groupCountY, groupCountZ);
    else ADD_COMMAND(TraceRays, groupCountX, groupCountY, groupCountZ);
}

void RHICommandList::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) 
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->Draw(vertexCount, instanceCount, firstVertex, firstInstance);
    else ADD_COMMAND(Draw, vertexCount, instanceCount, firstVertex, firstInstance);
}

void RHICommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance) 
{   
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->DrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    else ADD_COMMAND(DrawIndexed, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void RHICommandList::DrawIndirect(RHIBufferRef argumentBuffer, uint32_t offset, uint32_t drawCount) 
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->DrawIndirect(argumentBuffer, offset, drawCount);
    else ADD_COMMAND(DrawIndirect, argumentBuffer, offset, drawCount);
}

void RHICommandList::DrawIndexedIndirect(RHIBufferRef argumentBuffer, uint32_t offset, uint32_t drawCount)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->DrawIndexedIndirect(argumentBuffer, offset, drawCount);
    else ADD_COMMAND(DrawIndexedIndirect, argumentBuffer, offset, drawCount);
}

void RHICommandList::ImGuiCreateFontsTexture()
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->ImGuiCreateFontsTexture();
    else ADD_COMMAND(ImGuiCreateFontsTexture);
}

void RHICommandList::ImGuiRenderDrawData(ImGuiDrawFunc func)
{
    COMMANDLIST_DEBUG_OUTPUT();
    if(info.byPass) info.context->ImGuiRenderDrawData(func);
    else ADD_COMMAND(ImGuiRenderDrawData, func);
}


void RHICommandListImmediate::Flush()
{
    // LOG_DEBUG("RHICommandListImmediate Flushed.");
    for (int32_t i = 0; i < commands.size(); i++) 
    {
        commands[i]->Execute(info.context);
        delete commands[i];
    }
    commands.clear();

    info.context->Flush();
}

void RHICommandListImmediate::TextureBarrier(const RHITextureBarrier& barrier)
{
    ADD_COMMAND_IMMEDIATE(TextureBarrier, barrier);
}

void RHICommandListImmediate::BufferBarrier(const RHIBufferBarrier& barrier)
{
    ADD_COMMAND_IMMEDIATE(BufferBarrier, barrier);
}

void RHICommandListImmediate::CopyTextureToBuffer(RHITextureRef src, TextureSubresourceLayers srcSubresource, RHIBufferRef dst, uint64_t dstOffset)
{
    ADD_COMMAND_IMMEDIATE(CopyTextureToBuffer, src, srcSubresource, dst, dstOffset);
}

void RHICommandListImmediate::CopyBufferToTexture(RHIBufferRef src, uint64_t srcOffset, RHITextureRef dst, TextureSubresourceLayers dstSubresource)
{
    ADD_COMMAND_IMMEDIATE(CopyBufferToTexture, src, srcOffset, dst, dstSubresource);
}

void RHICommandListImmediate::CopyBuffer(RHIBufferRef src, uint64_t srcOffset, RHIBufferRef dst, uint64_t dstOffset, uint64_t size)
{
    ADD_COMMAND_IMMEDIATE(CopyBuffer, src, srcOffset, dst, dstOffset, size);
}

void RHICommandListImmediate::CopyTexture(RHITextureRef src, TextureSubresourceLayers srcSubresource, RHITextureRef dst, TextureSubresourceLayers dstSubresource)
{
    ADD_COMMAND_IMMEDIATE(CopyTexture, src, srcSubresource, dst, dstSubresource);
}

void RHICommandListImmediate::GenerateMips(RHITextureRef src)
{
    ADD_COMMAND_IMMEDIATE(GenerateMips, src);   
}


void RHICommandBeginCommand::Execute(RHICommandContextRef context) { context->BeginCommand(); }

void RHICommandEndCommand::Execute(RHICommandContextRef context) { context->EndCommand(); }

void RHICommandTextureBarrier::Execute(RHICommandContextRef context) { context->TextureBarrier(barrier); }

void RHICommandBufferBarrier::Execute(RHICommandContextRef context) { context->BufferBarrier(barrier); }

void RHICommandCopyTextureToBuffer::Execute(RHICommandContextRef context) { context->CopyTextureToBuffer(src, srcSubresource, dst, dstOffset); }

void RHICommandCopyBufferToTexture::Execute(RHICommandContextRef context) { context->CopyBufferToTexture(src, srcOffset, dst, dstSubresource); }

void RHICommandCopyBuffer::Execute(RHICommandContextRef context) { context->CopyBuffer(src, srcOffset, dst, dstOffset, size); }

void RHICommandCopyTexture::Execute(RHICommandContextRef context) { context->CopyTexture(src, srcSubresource, dst, dstSubresource); }

void RHICommandGenerateMips::Execute(RHICommandContextRef context) { context->GenerateMips(src); }

void RHICommandBuildTLAS::Execute(RHICommandContextRef context) { context->BuildTopLevelAccelerationStructure(tlas); }

void RHICommandPushEvent::Execute(RHICommandContextRef context) { context->PushEvent(name, color); }

void RHICommandPopEvent::Execute(RHICommandContextRef context) { context->PopEvent(); }

// void RHICommandBeginRendering::Execute(RHICommandContextRef context) { context->BeginRendering(rendering); }

void RHICommandBeginRendering::Execute(RHICommandContextRef context) { context->BeginRendering(rendering); }

void RHICommandEndRendering::Execute(RHICommandContextRef context) { context->EndRendering(); }

void RHICommandSetViewport::Execute(RHICommandContextRef context) { context->SetViewport(min, max); }

void RHICommandSetScissor::Execute(RHICommandContextRef context) { context->SetScissor(min, max); }

void RHICommandClearScissors::Execute(RHICommandContextRef context) { context->ClearScissors(attachments, scissors, baseArrayLayer, layerCount); }

void RHICommandSetDepthBias::Execute(RHICommandContextRef context) { context->SetDepthBias(constantBias, slopeBias, clampBias); }

void RHICommandSetLineWidth::Execute(RHICommandContextRef context) { context->SetLineWidth(width); }

void RHICommandSetGraphicsPipeline::Execute(RHICommandContextRef context) { context->SetGraphicsPipeline(graphicsPipeline); }

void RHICommandSetComputePipeline::Execute(RHICommandContextRef context) { context->SetComputePipeline(computePipeline); }

void RHICommandSetRayTracingPipeline::Execute(RHICommandContextRef context) { context->SetRayTracingPipeline(rayTracingPipeline); }

void RHICommandPushConstants::Execute(RHICommandContextRef context) { context->PushConstants(&data[0], size, frequency); }

void RHICommandBindDescriptorSet::Execute(RHICommandContextRef context) { context->BindDescriptorSet(descriptor, set); }

void RHICommandBindVertexBuffer::Execute(RHICommandContextRef context) { context->BindVertexBuffer(vertexBuffer, streamIndex, offset); }

void RHICommandBindIndexBuffer::Execute(RHICommandContextRef context) { context->BindIndexBuffer(indexBuffer, offset); }

void RHICommandDispatch::Execute(RHICommandContextRef context) { context->Dispatch(groupCountX, groupCountY, groupCountZ); }

void RHICommandDispatchIndirect::Execute(RHICommandContextRef context) { context->DispatchIndirect(argumentBuffer, argumentOffset); }

void RHICommandTraceRays::Execute(RHICommandContextRef context) { context->TraceRays(groupCountX, groupCountY, groupCountZ); }

void RHICommandDraw::Execute(RHICommandContextRef context) { context->Draw(vertexCount, instanceCount, firstVertex, firstInstance); }

void RHICommandDrawIndexed::Execute(RHICommandContextRef context) { context->DrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance); }

void RHICommandDrawIndirect::Execute(RHICommandContextRef context) { context->DrawIndirect(argumentBuffer, offset, drawCount); }

void RHICommandDrawIndexedIndirect::Execute(RHICommandContextRef context) { context->DrawIndexedIndirect(argumentBuffer, offset, drawCount); }

void RHICommandImGuiCreateFontsTexture::Execute(RHICommandContextRef context) { context->ImGuiCreateFontsTexture(); }

void RHICommandImGuiRenderDrawData::Execute(RHICommandContextRef context) { context->ImGuiRenderDrawData(func); }

void RHICommandImmediateTextureBarrier::Execute(RHICommandContextImmediateRef context) { context->TextureBarrier(barrier); }

void RHICommandImmediateBufferBarrier::Execute(RHICommandContextImmediateRef context) { context->BufferBarrier(barrier); }

void RHICommandImmediateCopyTextureToBuffer::Execute(RHICommandContextImmediateRef context) { context->CopyTextureToBuffer(src, srcSubresource, dst, dstOffset); }

void RHICommandImmediateCopyBufferToTexture::Execute(RHICommandContextImmediateRef context) { context->CopyBufferToTexture(src, srcOffset, dst, dstSubresource); }

void RHICommandImmediateCopyBuffer::Execute(RHICommandContextImmediateRef context) { context->CopyBuffer(src, srcOffset, dst, dstOffset, size); }

void RHICommandImmediateCopyTexture::Execute(RHICommandContextImmediateRef context) { context->CopyTexture(src, srcSubresource, dst, dstSubresource); }

void RHICommandImmediateGenerateMips::Execute(RHICommandContextImmediateRef context) { context->GenerateMips(src); }