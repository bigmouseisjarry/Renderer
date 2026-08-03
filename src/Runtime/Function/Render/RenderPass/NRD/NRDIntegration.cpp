/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include "Core/Log/Log.h"
#include "Function/Global/EngineContext.h"
#include "Function/Render/RDG/RDGHandle.h"
#include "Function/Render/RHI/RHIStructs.h"
#include "Function/Render/RenderResource/Texture.h"
#include "NRDIntegration.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#    include <malloc.h>
#else
#    include <alloca.h>
#endif

#define NRD_INTEGRATION_RETURN_FALSE_ON_FAILURE(expr) \
    if ((expr) != nri::Result::SUCCESS) \
    return false

constexpr uint32_t RANGE_TEXTURES = 0;
constexpr uint32_t RANGE_STORAGES = 1;

constexpr std::array<RHIFormat, (size_t)nrd::Format::MAX_NUM> NrdFormatToRHI = {

    FORMAT_R8_UNORM,
    FORMAT_R8_SNORM,
    FORMAT_R8_UINT,
    FORMAT_R8_SINT,

    FORMAT_R8G8_UNORM,
    FORMAT_R8G8_SNORM,
    FORMAT_R8G8_UINT,
    FORMAT_R8G8_SINT,

    FORMAT_R8G8B8A8_UNORM,
    FORMAT_R8G8B8A8_SNORM,
    FORMAT_R8G8B8A8_UINT,
    FORMAT_R8G8B8A8_SINT,
    FORMAT_R8G8B8A8_SRGB,

    FORMAT_R16_UNORM,
    FORMAT_R16_SNORM,
    FORMAT_R16_UINT,
    FORMAT_R16_SINT,
    FORMAT_R16_SFLOAT,

    FORMAT_R16G16_UNORM,
    FORMAT_R16G16_SNORM,
    FORMAT_R16G16_UINT,
    FORMAT_R16G16_SINT,
    FORMAT_R16G16_SFLOAT,

    FORMAT_R16G16B16A16_UNORM,
    FORMAT_R16G16B16A16_SNORM,  
    FORMAT_R16G16B16A16_UINT,
    FORMAT_R16G16B16A16_SINT,
    FORMAT_R16G16B16A16_SFLOAT,

    FORMAT_R32_UINT,
    FORMAT_R32_SINT,
    FORMAT_R32_SFLOAT,

    FORMAT_R32G32_UINT,
    FORMAT_R32G32_SINT,
    FORMAT_R32G32_SFLOAT,

    FORMAT_R32G32B32_UINT,
    FORMAT_R32G32B32_SINT,
    FORMAT_R32G32B32_SFLOAT,

    FORMAT_R32G32B32A32_UINT,
    FORMAT_R32G32B32A32_SINT,
    FORMAT_R32G32B32A32_SFLOAT,

    FORMAT_A2R10G10B10_UNORM,
    FORMAT_A2R10G10B10_UINT,
    FORMAT_B10G11R11_UFLOAT,
    FORMAT_E5B9G9R9_UFLOAT,
};

static inline uint16_t DivideUp(uint32_t x, uint16_t y) {
    return uint16_t((x + y - 1) / y);
}

static inline RHIFormat GetRHIFormat(nrd::Format format) {
    return NrdFormatToRHI[(uint32_t)format];
}

template <typename T, typename A>
constexpr T Align(const T& size, A alignment) {
    return T(((size + alignment - 1) / alignment) * alignment);
}

nrd::Result NRDIntegration::Recreate(const NRDIntegrationCreationDesc& integrationDesc, const nrd::InstanceCreationDesc& instanceDesc) {

    if (m_SkipDestroy)
        m_SkipDestroy = false;
    else
        Destroy();

    m_Desc = integrationDesc;

    const nrd::LibraryDesc& libraryDesc = *nrd::GetLibraryDesc();
    ENGINE_LOG_INFO("NRD Normal Encoding [{}] ", (uint32_t)libraryDesc.normalEncoding); // 1
    ENGINE_LOG_INFO("NRD Roughness Encoding [{}] ", (uint32_t)libraryDesc.roughnessEncoding);   // 1
    if (libraryDesc.versionMajor != NRD_VERSION_MAJOR || libraryDesc.versionMinor != NRD_VERSION_MINOR) {
        LOG_FATAL("NRD version mismatch detected!");
        return nrd::Result::FAILURE;
    }

    nrd::Result result = CreateInstance(instanceDesc, m_Instance);
    if (result == nrd::Result::SUCCESS) {
        result = _CreateResources() ? nrd::Result::SUCCESS : nrd::Result::FAILURE;
        if (result == nrd::Result::SUCCESS)
            result = RecreatePipelines() ? nrd::Result::SUCCESS : nrd::Result::FAILURE;
    }

    if (result != nrd::Result::SUCCESS)
        Destroy();

    return result;
}

bool NRDIntegration::RecreatePipelines() {

    const nrd::InstanceDesc& instanceDesc = *GetInstanceDesc(*m_Instance);

    m_Pipelines.clear();
    for (uint32_t i = 0; i < instanceDesc.pipelinesNum; i++) {
        const nrd::PipelineDesc& nrdPipelineDesc = instanceDesc.pipelines[i];
        const nrd::ComputeShaderDesc& nrdComputeShader = nrdPipelineDesc.computeShaderSPIRV; 

        std::vector<uint8_t> code;
        code.resize(nrdComputeShader.size);
        memcpy(code.data(), nrdComputeShader.bytecode, nrdComputeShader.size);
        RHIShaderInfo shaderInfo = {
            .entry = instanceDesc.shaderEntryPoint,
            .frequency = SHADER_FREQUENCY_COMPUTE,
            .code = code
        };
        RHIShaderRef shader = EngineContext::RHI()->CreateShader(shaderInfo);

        RHIComputePipelineInfo pipelineInfo     = {};
        pipelineInfo.rootSignature              = m_RootSignature;
        pipelineInfo.computeShader              = shader; 
        RHIComputePipelineRef computePipeline   = EngineContext::RHI()->CreateComputePipeline(pipelineInfo);
        m_Pipelines.push_back(computePipeline);
    }

    return true;
}

bool NRDIntegration::_CreateResources() {

    auto& backend = EngineContext::RHI();
    const nrd::InstanceDesc& instanceDesc = *GetInstanceDesc(*m_Instance);
    const uint32_t poolSize = instanceDesc.permanentPoolSize + instanceDesc.transientPoolSize;

    { // Texture pool
        // No reallocation, please!
        m_TexturePool.resize(poolSize);

        for (uint32_t i = 0; i < poolSize; i++) {
            // Create NRI texture
            char name[128];
    
            RHITextureRef texture = nullptr;
            const nrd::TextureDesc& nrdTextureDesc = (i < instanceDesc.permanentPoolSize) ? instanceDesc.permanentPool[i] : instanceDesc.transientPool[i - instanceDesc.permanentPoolSize];
            {
                RHIFormat format = GetRHIFormat(nrdTextureDesc.format);
                if (m_Desc.promoteFloat16to32) {
                    if (format == FORMAT_R16_SFLOAT)
                        format = FORMAT_R32_SFLOAT;
                    else if (format == FORMAT_R16G16_SFLOAT)
                        format = FORMAT_R32G32_SFLOAT;
                    else if (format == FORMAT_R16G16B16A16_SFLOAT)
                        format = FORMAT_R32G32B32A32_SFLOAT;
                } else if (m_Desc.demoteFloat32to16) {
                    if (format == FORMAT_R32_SFLOAT)
                        format = FORMAT_R16_SFLOAT;
                    else if (format == FORMAT_R32G32_SFLOAT)
                        format = FORMAT_R16G16_SFLOAT;
                    else if (format == FORMAT_R32G32B32A32_SFLOAT)
                        format = FORMAT_R16G16B16A16_SFLOAT;
                }

                uint16_t w = DivideUp(m_Desc.resourceWidth, nrdTextureDesc.downsampleFactor);
                uint16_t h = DivideUp(m_Desc.resourceHeight, nrdTextureDesc.downsampleFactor);

                texture = backend->CreateTexture({
                    .format = format,
                    .extent = {w, h, 1},
                    .arrayLayers = 1,
                    .mipLevels = 1,
                    .memoryUsage = MEMORY_USAGE_GPU_ONLY,
                    .type = RESOURCE_TYPE_RW_TEXTURE | RESOURCE_TYPE_TEXTURE
                });

                if (i < instanceDesc.permanentPoolSize)
                    snprintf(name, sizeof(name), "%s::P(%u)", m_Desc.name, i);
                else
                    snprintf(name, sizeof(name), "%s::T(%u)", m_Desc.name, i - instanceDesc.permanentPoolSize);
                //m_iCore.SetDebugName(texture, name);
            }

            { // Construct NRD texture
                NRDResource& resource = m_TexturePool[i];
                resource.texture = texture;
                resource.state = RESOURCE_STATE_UNDEFINED;
            }
        }
    }

    { // Constant buffer (Uniform buffer)
        m_ConstantBufferViewSize = Align(instanceDesc.constantBufferMaxDataSize, 64);  // 0x40, minUniformBufferOffsetAlignment
        m_ConstantBufferSize = uint64_t(m_ConstantBufferViewSize) * instanceDesc.descriptorPoolDesc.setsMaxNum * m_Desc.queuedFrameNum;

        RHIBufferInfo bufferInfo = {};
        bufferInfo.size = m_ConstantBufferSize;
        bufferInfo.type = RESOURCE_TYPE_RW_BUFFER | RESOURCE_TYPE_UNIFORM_BUFFER | RESOURCE_TYPE_BUFFER;
        bufferInfo.memoryUsage = MEMORY_USAGE_CPU_TO_GPU;
        m_ConstantBuffer = backend->CreateBuffer(bufferInfo);
    }

    {   // Sampler
        m_Samplers.clear();
        for (uint32_t i = 0; i < instanceDesc.samplersNum; i++) {

            nrd::Sampler nrdSampler = instanceDesc.samplers[i];

            RHISamplerInfo samplerInfo = {};
            samplerInfo.minFilter = nrdSampler == nrd::Sampler::NEAREST_CLAMP ? FILTER_TYPE_NEAREST : FILTER_TYPE_LINEAR;
            samplerInfo.magFilter = nrdSampler == nrd::Sampler::NEAREST_CLAMP ? FILTER_TYPE_NEAREST : FILTER_TYPE_LINEAR;
            samplerInfo.addressModeU = ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.mipmapMode = MIPMAP_MODE_NEAREST; 
            RHISamplerRef sampler = backend->CreateSampler(samplerInfo);

            m_Samplers.push_back(sampler);
        }
    }

    // Pipeline layout
    {
        const nrd::LibraryDesc& nrdLibraryDesc = *nrd::GetLibraryDesc();
        uint32_t constantBufferAndSamplersSpaceIndex = instanceDesc.constantBufferAndSamplersSpaceIndex;        //1
        uint32_t resourcesSpaceIndex = instanceDesc.resourcesSpaceIndex;                                        //0
        uint32_t constantBufferRegisterIndex = instanceDesc.constantBufferRegisterIndex;                        //0
        uint32_t samplersBaseRegisterIndex = instanceDesc.samplersBaseRegisterIndex;                            //0
        uint32_t resourcesBaseRegisterIndex = instanceDesc.resourcesBaseRegisterIndex;                          //0
        uint32_t constantBufferOffset = nrdLibraryDesc.spirvBindingOffsets.constantBufferOffset;                //2     + instanceDesc.constantBufferRegisterIndex
        uint32_t samplerOffset = nrdLibraryDesc.spirvBindingOffsets.samplerOffset;                              //0     + instanceDesc.samplersBaseRegisterIndex + i
        uint32_t textureOffset = nrdLibraryDesc.spirvBindingOffsets.textureOffset;                              //20    + instanceDesc.resourcesBaseRegisterIndex
        uint32_t storageTextureOffset = nrdLibraryDesc.spirvBindingOffsets.storageTextureAndBufferOffset;       //3     + instanceDesc.resourcesBaseRegisterIndex

        // 此处的布局可以使用SPIRV二进制反射得到（存在RHIShader的ShaderReflectInfo里）
        RHIRootSignatureInfo rootSignatureInfo = {};
        for(uint32_t i = storageTextureOffset; i < textureOffset; i++)
            rootSignatureInfo.AddEntry({resourcesSpaceIndex, i, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_RW_TEXTURE});
        for(uint32_t i = textureOffset; i <= 100; i++)
            rootSignatureInfo.AddEntry({resourcesSpaceIndex, i, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_TEXTURE});
        for(uint32_t i = samplerOffset; i <= 1; i++)
            rootSignatureInfo.AddEntry({constantBufferAndSamplersSpaceIndex, i, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_SAMPLER});
        rootSignatureInfo.AddEntry({constantBufferAndSamplersSpaceIndex, constantBufferOffset, 1, SHADER_FREQUENCY_COMPUTE, RESOURCE_TYPE_UNIFORM_BUFFER});
              
        m_RootSignature = backend->CreateRootSignature(rootSignatureInfo);
    }

    return true;
}

void NRDIntegration::NewFrame() {

    // Must be here since the initial value is "-1", otherwise "descriptorPool[0]" will be used twice on the 1st and 2nd frames
    m_FrameIndex++;

    // Current descriptor pool index
    m_DescriptorPoolIndex = m_FrameIndex % m_Desc.queuedFrameNum;

    m_PrevFrameIndexFromSettings++;
}

nrd::Result NRDIntegration::SetCommonSettings(const nrd::CommonSettings& commonSettings) {

    nrd::Result result = nrd::SetCommonSettings(*m_Instance, commonSettings);
    assert(result == nrd::Result::SUCCESS);

    if (m_FrameIndex == 0 || commonSettings.accumulationMode != nrd::AccumulationMode::CONTINUE)
        m_PrevFrameIndexFromSettings = commonSettings.frameIndex;

    return result;
}

nrd::Result NRDIntegration::SetDenoiserSettings(nrd::Identifier denoiser, const void* denoiserSettings) {

    nrd::Result result = nrd::SetDenoiserSettings(*m_Instance, denoiser, denoiserSettings);
    assert(result == nrd::Result::SUCCESS);

    return result;
}

void NRDIntegration::Denoise(RDGBuilder& builder, const nrd::Identifier* denoisers, uint32_t denoisersNum, NRDResourceSnapshot& resourceSnapshot) {

    // Save initial state
    // std::vector<RHIResourceState> initialStates(resourceSnapshot.uniqueNum);
    // for (size_t i = 0; i < resourceSnapshot.uniqueNum; i++)
    //     initialStates[i] = resourceSnapshot.unique[i].state;

    // One time sanity check
    // if (m_FrameIndex == 0) {
    //     RHITextureRef normalRoughnessTexture = resourceSnapshot.slots[(size_t)nrd::ResourceType::IN_NORMAL_ROUGHNESS]->texture;
    //     RHIFormat format = normalRoughnessTexture->GetInfo().format;
    //     const nrd::LibraryDesc& nrdLibraryDesc = *nrd::GetLibraryDesc();

    //     bool isNormalRoughnessFormatValid = false;
    //     switch (nrdLibraryDesc.normalEncoding) {
    //         case nrd::NormalEncoding::RGBA8_UNORM:
    //             isNormalRoughnessFormatValid = format == FORMAT_R8G8B8A8_UNORM;
    //             break;
    //         case nrd::NormalEncoding::RGBA8_SNORM:
    //             isNormalRoughnessFormatValid = format == FORMAT_R8G8B8A8_SNORM;
    //             break;
    //         case nrd::NormalEncoding::R10_G10_B10_A2_UNORM:
    //             isNormalRoughnessFormatValid = format == FORMAT_A2R10G10B10_UNORM; 
    //             break;
    //         case nrd::NormalEncoding::RGBA16_UNORM:
    //             isNormalRoughnessFormatValid = format == FORMAT_R16G16B16A16_UNORM;
    //             break;
    //         case nrd::NormalEncoding::RGBA16_SNORM:
    //             isNormalRoughnessFormatValid = format == FORMAT_R16G16B16A16_SNORM || 
    //                                            format == FORMAT_R16G16B16A16_SFLOAT || 
    //                                            format == FORMAT_R32G32B32A32_SFLOAT;
    //             break;
    //         default:
    //             break;
    //     }
    //     assert(isNormalRoughnessFormatValid);
    // }

    // ReBlur里有个REBLUR_FORMAT_PREV_NORMAL_ROUGHNESS宏是靠cmake定义的，需要跟着改！！！

    // 注册RDG资源
    RDGBufferHandle bufferHandle = builder.CreateBuffer("NRD Constant Buffer")
        .Import(m_ConstantBuffer, RESOURCE_STATE_UNDEFINED)
        .Finish();
    for(int i = 0; i < m_TexturePool.size(); i++)
    {
        NRDResource& resource = m_TexturePool[i];
        resource.handle = builder.CreateTexture("NRD Texture [" + std::to_string(i) + "]")
            .Import(resource.texture, resource.state) 
            .Finish();
    }

    // Retrieve dispatches
    const nrd::DispatchDesc* dispatchDescs = nullptr;
    uint32_t dispatchDescsNum = 0;
    GetComputeDispatches(*m_Instance, denoisers, denoisersNum, dispatchDescs, dispatchDescsNum);

    // Invoke dispatches
    for (uint32_t i = 0; i < dispatchDescsNum; i++) {   //RELAX 0 0 3~9 10 10 10
        const nrd::DispatchDesc& dispatchDesc = dispatchDescs[i];
        _Dispatch(builder, dispatchDesc, resourceSnapshot);
    }
}

void NRDIntegration::_Dispatch(RDGBuilder& builder, const nrd::DispatchDesc& dispatchDesc, NRDResourceSnapshot& resourceSnapshot) {
    const nrd::InstanceDesc& instanceDesc = *GetInstanceDesc(*m_Instance);
    const nrd::PipelineDesc& pipelineDesc = instanceDesc.pipelines[dispatchDesc.pipelineIndex];

    auto passBuilder = builder.CreateComputePass("NRD Pass [" + std::to_string(dispatchDesc.pipelineIndex) + "]");
    RDGBufferHandle bufferHandle = builder.GetBuffer("NRD Constant Buffer");

    // Update constants
    uint32_t dynamicConstantBufferOffset = m_ConstantBufferOffsetPrev;
    {
        // Stream data only if needed
        if (dispatchDesc.constantBufferDataSize && !dispatchDesc.constantBufferDataMatchesPreviousDispatch) {
            // Ring-buffer logic
            if (m_ConstantBufferOffset + m_ConstantBufferViewSize > m_ConstantBufferSize)
                m_ConstantBufferOffset = 0;

            dynamicConstantBufferOffset = m_ConstantBufferOffset;
            m_ConstantBufferOffset += m_ConstantBufferViewSize;

            // Upload CB data
            memcpy((uint8_t*)m_ConstantBuffer->Map() + dynamicConstantBufferOffset, dispatchDesc.constantBufferData, dispatchDesc.constantBufferDataSize);

            // Save previous offset for potential CB data reuse
            m_ConstantBufferOffsetPrev = dynamicConstantBufferOffset;
        }
        if (dispatchDesc.constantBufferDataSize)
            passBuilder.Read(1, 2, 0, bufferHandle, dynamicConstantBufferOffset, dispatchDesc.constantBufferDataSize);
    }

    // Fill descriptors
    {
        uint32_t n = 0;
        for (uint32_t i = 0; i < pipelineDesc.resourceRangesNum; i++) {
            const nrd::ResourceRangeDesc& resourceRange = pipelineDesc.resourceRanges[i];
            const bool isStorage = resourceRange.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE;

            uint32_t baseBinding = isStorage ? 3 : 20;
            uint32_t bindingOffset = 0;          
            for (uint32_t j = 0; j < resourceRange.descriptorsNum; j++) {
                const nrd::ResourceDesc& resourceDesc = dispatchDesc.resources[n++];    // TODO ?? n++

                // Get resource
                // 绑定资源，有内部和外部区别
                NRDResource* resource = nullptr;
                if (resourceDesc.type == nrd::ResourceType::TRANSIENT_POOL)
                    resource = &m_TexturePool[resourceDesc.indexInPool + instanceDesc.permanentPoolSize];
                else if (resourceDesc.type == nrd::ResourceType::PERMANENT_POOL)
                    resource = &m_TexturePool[resourceDesc.indexInPool];
                else {
                    resource = &resourceSnapshot.slots[(uint32_t)resourceDesc.type];
                }
                if(resource->handle.ID() == UINT32_MAX) 
                    assert(false);   
                
                if(isStorage)
                {
                    passBuilder.ReadWrite(0, baseBinding + bindingOffset, 0, resource->handle);
                    resource->state = RESOURCE_STATE_UNORDERED_ACCESS;
                }
                else
                {
                    passBuilder.Read(0, baseBinding + bindingOffset, 0, resource->handle);
                    resource->state = RESOURCE_STATE_SHADER_RESOURCE;
                }    
                bindingOffset++; 
            }
        }
    }

    // Rendering
    RDGComputePassHandle pass = passBuilder
        .RootSignature(m_RootSignature)
        .PassIndex(dispatchDesc.gridWidth, dispatchDesc.gridHeight, dispatchDesc.pipelineIndex)
        .Sampler(1, 0, 0, m_Samplers[0])
        .Sampler(1, 1, 0, m_Samplers[1])
        .Execute([&](RDGPassContext context) {       

            uint32_t width = context.passIndex[0];
            uint32_t height = context.passIndex[1];
            uint32_t pipelineIndex = context.passIndex[2];

            RHICommandListRef command = context.command; 
            command->SetComputePipeline(m_Pipelines[pipelineIndex]);
            if(context.descriptors[0])
                command->BindDescriptorSet(context.descriptors[0], 0);
            command->BindDescriptorSet(context.descriptors[1], 1);
            command->Dispatch(  width, 
                                height, 
                                1);
        })
        .Finish();
}

void NRDIntegration::Destroy() {

    if (m_Instance)
        DestroyInstance(*m_Instance);

    // Better keep in sync with the default values used by constructor
    m_TexturePool.clear();
    m_Pipelines.clear();
    m_Desc = {};
    m_Instance = nullptr;
    m_ConstantBufferSize = 0;
    m_ConstantBufferViewSize = 0;
    m_ConstantBufferOffset = 0;
    m_ConstantBufferOffsetPrev = 0;
    m_DescriptorPoolIndex = 0;
    m_FrameIndex = uint32_t(-1);
    m_PrevFrameIndexFromSettings = 0;
    m_SkipDestroy = false;
}
