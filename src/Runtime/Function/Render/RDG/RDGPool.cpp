#include "RDGPool.h"
#include "Core/Log/Log.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RHI/RHIResource.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <cassert>
#include <cstdint>

RDGBufferPool::PooledBuffer RDGBufferPool::Allocate(const RHIBufferInfo& info)
{
    RDGBufferPool::PooledBuffer ret;

    auto& buffers = pooledBuffers[info];
    for(auto iter = buffers.begin(); iter != buffers.end(); iter++) 
    {
        if(iter->buffer->GetInfo().size >= info.size)   // 搜索符合条件的，尺寸
        {
            ret = *iter;
            buffers.erase(iter);
            pooledSize--;
            return ret;
        }
    }
    
    LOG_DEBUG("RHIBuffer not found in cache, creating new.");
    ret = {
        .buffer = EngineContext::RHI()->CreateBuffer(info),                 // 没找到，创建新buffer
        .state = RESOURCE_STATE_UNDEFINED
    };
    allocatedSize++;

    return ret;
}

void RDGBufferPool::Release(const RDGBufferPool::PooledBuffer& pooledBuffer)
{
    pooledBuffers[pooledBuffer.buffer->GetInfo()].push_back(pooledBuffer);
    pooledSize++;
}

RDGTexturePool::PooledTexture RDGTexturePool::Allocate(const RHITextureInfo& info)
{
    RDGTexturePool::PooledTexture ret;

    assert(info.mipLevels != 0);

    auto& textures = pooledTextures[info];
    for(auto iter = textures.begin(); iter != textures.end(); iter++) 
    {
        ret = *iter;                    
        textures.erase(iter);
        pooledSize--;
        return ret;
    }
    
    LOG_DEBUG("RHITexture not found in cache, creating new.");
    ret = {
        .texture = EngineContext::RHI()->CreateTexture(info),
        .state = RESOURCE_STATE_UNDEFINED,
        .queueFamily = RHI_QUEUE_FAMILY_IGNORED,    // 新建纹理无归属族
    };
    allocatedSize++;

    return ret;
}

void RDGTexturePool::Release(const RDGTexturePool::PooledTexture& pooledTexture)
{
    pooledTextures[pooledTexture.texture->GetInfo()].push_back(pooledTexture);
    pooledSize++;
}

RDGTextureViewPool::PooledTextureView RDGTextureViewPool::Allocate(const RHITextureViewInfo& info)
{   
    RHITextureViewInfo actualInfo = info;   // RHI计算的时候也会用默认subresource替换，需要避免分配和返回的info不一致
    if(actualInfo.subresource.aspect == TEXTURE_ASPECT_NONE)  actualInfo.subresource = actualInfo.texture->GetDefaultSubresourceRange();    

    RDGTextureViewPool::PooledTextureView ret;

    auto& textureViews = pooledTextureViews[actualInfo];
    for(auto iter = textureViews.begin(); iter != textureViews.end(); iter++) 
    {
        ret = *iter;                    
        textureViews.erase(iter);
        pooledSize--;
        return ret;
    }
    
    LOG_DEBUG("RHITextureView not found in cache, creating new.");
    ret = {
        .textureView = EngineContext::RHI()->CreateTextureView(actualInfo)   
    };
    allocatedSize++;

    return ret;
}

void RDGTextureViewPool::Release(const RDGTextureViewPool::PooledTextureView& pooledTextureView)
{
    pooledTextureViews[pooledTextureView.textureView->GetInfo()].push_back(pooledTextureView);
    pooledSize++;
}


RDGDescriptorSetPool::PooledDescriptor RDGDescriptorSetPool::Allocate(const Key& key, const RHIRootSignatureRef& rootSignature)
{
    RDGDescriptorSetPool::PooledDescriptor ret;

    auto& descriptors = pooledDescriptors[key];     // 桶=足迹键;miss即该足迹首帧,走创建
    for(auto iter = descriptors.begin(); iter != descriptors.end(); iter++)
    {
        ret = *iter;
        descriptors.erase(iter);
        pooledSize--;
        break;
    }

    if (ret.descriptor == nullptr)
    {
        LOG_DEBUG("RHIDescriptorSet not found in cache, creating new.");
        ret = {
            .descriptor = rootSignature->CreateDescriptorSet(key.set)
        };
        allocatedSize++;
    }

    // 静态采样器幂等重播：池键只含entries，同键异表RS共用池条目时保证本分配者的采样器生效
    // （CreateDescriptorSet创建期已播一次；重播成本=每pass几个写，与旧pass->samplers旁路等价）
    for (const RHIStaticSampler& staticSampler : rootSignature->GetInfo().GetStaticSamplers())
    {
        if (staticSampler.set != key.set || staticSampler.sampler == nullptr) continue;
        ret.descriptor->UpdateDescriptor({
            .binding = staticSampler.binding,
            .index = staticSampler.index,
            .resourceType = RESOURCE_TYPE_SAMPLER,
            .sampler = staticSampler.sampler});
    }

    return ret;
}

void RDGDescriptorSetPool::Release(const Key& key, const PooledDescriptor& pooledDescriptor)
{
    pooledDescriptors[key].push_back(pooledDescriptor);
    pooledSize++;
}