#pragma once

// Dependencies
#include "Function/Render/RDG/RDGBuilder.h"
#include "Function/Render/RHI/RHICommandList.h"
#include "Function/Render/RHI/RHIStructs.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

#include "NRD.h"

struct NRDResource {

    NRDResource(){}

    NRDResource(RDGTextureHandle handle)
    : handle(handle)
    {}

    RDGTextureHandle handle;
    RHITextureRef texture;
    RHIResourceState state = RESOURCE_STATE_UNDEFINED;
};

//===================================================================================================
// Resource snapshot = collection of resources
//===================================================================================================

// Represents the state of resources at the current moment:
//  - must contain valid entries for "resource types" referenced by a "Denoise" call
//  - if you know what you do, same resource may be used several times for different slots
//  - if "restoreInitialState" is "false":
//      - "Denoise" call modifies resource states, use "userArg" to assosiate "state" with an app resource
//      - update app-side resource states using "Resources::unique[0:uniqueNum]" entries
struct NRDResourceSnapshot {
    std::array<NRDResource, (size_t)nrd::ResourceType::MAX_NUM - 2> slots = {};

    inline void SetResource(nrd::ResourceType slot, const NRDResource& resource) {
        slots[(size_t)slot] = resource;
    }

    // The structure stores pointers to itself, thus can't be copied
    NRDResourceSnapshot(const NRDResourceSnapshot&) = delete;
    NRDResourceSnapshot& operator=(const NRDResourceSnapshot&) = delete;
    NRDResourceSnapshot() = default;
};

//===================================================================================================
// Integration instance
//===================================================================================================

struct NRDIntegrationCreationDesc {
    // Not so long name
    char name[64] = "";

    // Resource dimensions
    uint16_t resourceWidth = 0;
    uint16_t resourceHeight = 0;

    // (1-3 usually) the application must provide number of queued frames, it's needed to guarantee
    // that constant data and descriptor sets are not overwritten while being executed on the GPU
    uint8_t queuedFrameNum = 3;

    // Demote FP32 to FP16 (slightly improves performance in exchange of precision loss)
    // (FP32 is used only for viewZ under the hood, all denoisers are FP16 compatible)
    bool demoteFloat32to16 = false;

    // Promote FP16 to FP32 (overkill, kills performance)
    bool promoteFloat16to32 = false;

    // TODO: there can be an option for UNORM/SNORM 8/16-bit promotion/demotion, but only for resources
    // marked as history (i.e. not normal-roughness and internal data resources)
};

// Threadsafe: no
struct NRDIntegration {
    inline NRDIntegration() {
    }

    // Expects alive device
    inline ~NRDIntegration() {
        Destroy();
    }

    // Creation and re-creation, aka resize. "Destroy" is called under the hood
    nrd::Result Recreate(const NRDIntegrationCreationDesc& nrdIntegrationDesc, const nrd::InstanceCreationDesc& instanceCreationDesc);

    // Must be called once on a frame start
    void NewFrame();

    // Must be used instead of eponymous NRD API functions
    nrd::Result SetCommonSettings(const nrd::CommonSettings& commonSettings);
    nrd::Result SetDenoiserSettings(nrd::Identifier denoiser, const void* denoiserSettings);

    // Invoke denoising for specified denoisers. This function binds own descriptor heap (pool).
    // After the call "resourceSnapshot" entries will represent the "final" state of resources,
    // which must be used as "before" state in next "barrier" calls. The initial state of resources
    // can be restored by using "resourceSnapshot.restoreInitialState = true" (suboptimal).
    void Denoise(RDGBuilder& builder, const nrd::Identifier* denoisers, uint32_t denoisersNum, NRDResourceSnapshot& resourceSnapshot);

    // Destroy.
    // Device should have no NRD work in flight if "autoWaitForIdle = false"!
    void Destroy();

    // (Optional) Called under the hood, but can be used to explicitly reload pipelines.
    // Device should have no NRD work in flight if "autoWaitForIdle = false"!
    bool RecreatePipelines();

private:
    NRDIntegration(const NRDIntegration&) = delete;

    bool _CreateResources();
    void _Dispatch(RDGBuilder& builder, const nrd::DispatchDesc& dispatchDesc, NRDResourceSnapshot& resourceSnapshot);

    RHIBufferRef m_ConstantBuffer;
    std::vector<RHISamplerRef> m_Samplers;
    std::vector<NRDResource> m_TexturePool;
    RHIRootSignatureRef m_RootSignature;
    std::vector<RHIComputePipelineRef> m_Pipelines;

    NRDIntegrationCreationDesc m_Desc = {};

    nrd::Instance* m_Instance = nullptr;
    uint64_t m_ConstantBufferSize = 0;
    uint32_t m_ConstantBufferViewSize = 0;
uint32_t m_ConstantBufferOffset = 0;
    uint32_t m_ConstantBufferOffsetPrev = 0;
    uint32_t m_DescriptorPoolIndex = 0;
    uint32_t m_FrameIndex = uint32_t(-1); // 0 needed after 1st "NewFrame"
    uint32_t m_PrevFrameIndexFromSettings = 0;
    bool m_SkipDestroy = false;
};

