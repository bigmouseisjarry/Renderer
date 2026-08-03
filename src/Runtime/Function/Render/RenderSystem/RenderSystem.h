#pragma once

#include "Core/DependencyGraph/DependencyGraph.h"
#include "Function/Global/Definations.h"
#include "Function/Render/RenderPass/MeshPass.h"
#include "Function/Render/RenderPass/RenderPass.h"
#include "RenderLightManager.h"
#include "RenderMeshManager.h"
#include "RenderSurfaceCacheManager.h"

#include <array>
#include <memory>

static const Extent2D WINDOW_EXTENT = { WINDOW_WIDTH, WINDOW_HEIGHT };
static const Extent2D HALF_WINDOW_EXTENT = { HALF_WINDOW_WIDTH, HALF_WINDOW_HEIGHT };
static const RHIFormat HDR_COLOR_FORMAT = FORMAT_R16G16B16A16_SFLOAT;
static const RHIFormat COLOR_FORMAT = FORMAT_R8G8B8A8_UNORM;   
static const RHIFormat DEPTH_FORMAT = FORMAT_D32_SFLOAT;

class RenderSystem
{
public:
    void Init();
    void Destroy() {}
    void InitGLFW();
    void DestroyGLFW();

    bool Tick();
    void BuildRDG();
    void ExecuteRDG();

    void SetSurfaceCacheUpdate(bool update)                                                 { surfaceCacheManager->SetDynamicUpdate(update); }
    void SetSurfaceCacheFixScale(bool fixScale)                                             { surfaceCacheManager->SetFixScale(fixScale); }

    const std::array<std::shared_ptr<RenderPass>, PASS_TYPE_MAX_CNT>& GetPasses()           { return passes; }
    const std::array<std::shared_ptr<MeshPass>, MESH_PASS_TYPE_MAX_CNT>& GetMeshPasses()    { return meshPasses; }
    const bool IsPassEnabled(PassType passType) { if(passes[passType] && passes[passType]->IsEnabled()) return true; return false; }
    void SetPassEnabled(PassType passType, bool enable) { if(passes[passType]) passes[passType]->SetEnable(enable); }

    GLFWwindow* GetWindow()         { return window; }
    Extent2D GetWindowsExtent()     { return WINDOW_EXTENT; }
    Extent2D GetHalfWindowsExtent() { return HALF_WINDOW_EXTENT; }
    RHIFormat GetHdrColorFormat()   { return HDR_COLOR_FORMAT; } 
    RHIFormat GetColorFormat()      { return COLOR_FORMAT; } 
    RHIFormat GetDepthFormat()      { return DEPTH_FORMAT; }
    RHISwapchainRef GetSwapchain()  { return swapchain; }

    inline std::shared_ptr<RenderMeshManager> GetMeshManager()                  { return meshManager; }
    inline std::shared_ptr<RenderLightManager> GetLightManager()                { return lightManager; }
    inline std::shared_ptr<RenderSurfaceCacheManager> GetSurfaceCacheManager()  { return surfaceCacheManager; }
    RenderGlobalSetting* GetGlobalSetting()                                     { return &globalSetting; }
    DependencyGraphRef GetRDGDependenctyGraph()                                 { return rdgDependencyGraph; }

private:
    GLFWwindow* window;

    std::array<RDGBuilderRef, FRAMES_IN_FLIGHT> rdgBuilders;

    RHIBackendRef backend;
    RHISurfaceRef surface;
    RHIQueueRef queue;
    RHISwapchainRef swapchain;
    RHICommandPoolRef pool;
    

    struct PerFrameCommonResource 
    {
        RHICommandListRef command;
        RHISemaphoreRef startSemaphore;
        RHISemaphoreRef finishSemaphore;
        RHIFenceRef fence;
    };
    std::array<PerFrameCommonResource, FRAMES_IN_FLIGHT> perFrameCommonResources;
    RenderGlobalSetting globalSetting = {};
    DependencyGraphRef rdgDependencyGraph;

    std::array<std::shared_ptr<RenderPass>, PASS_TYPE_MAX_CNT> passes;
    std::array<std::shared_ptr<MeshPass>, MESH_PASS_TYPE_MAX_CNT> meshPasses;

    void InitPasses();
    void InitBaseResource();
    void SubmitRHI();
    void UpdateGlobalSetting();

    std::shared_ptr<RenderMeshManager> meshManager;
    std::shared_ptr<RenderLightManager> lightManager;
    std::shared_ptr<RenderSurfaceCacheManager> surfaceCacheManager;
};