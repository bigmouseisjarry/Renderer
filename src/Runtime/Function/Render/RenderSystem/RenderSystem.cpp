#include "RenderSystem.h"
#include "Function/Global/Definations.h"
#include "Function/Global/EngineContext.h"
#include "Function/Global/EngineThreadPool.h"
#include "Function/Render/RDG/RDGBuilder.h"
#include "Function/Render/RenderPass/GPUCullingPass.h"
#include "Function/Render/RenderPass/ClusterLightingPass.h"
#include "Function/Render/RenderPass/IBLPass.h"
#include "Function/Render/RenderPass/DepthPass.h"
#include "Function/Render/RenderPass/DepthPyramidPass.h"
#include "Function/Render/RenderPass/DirectionalShadowPass.h"
#include "Function/Render/RenderPass/PointShadowPass.h"
#include "Function/Render/RenderPass/ClipmapPass.h"
#include "Function/Render/RenderPass/DDGIPass.h"
#include "Function/Render/RenderPass/GBufferPass.h"
#include "Function/Render/RenderPass/SurfaceCachePass.h"
#include "Function/Render/RenderPass/ReprojectionPass.h"
#include "Function/Render/RenderPass/DeferredLightingPass.h"
#include "Function/Render/RenderPass/SSSRPass.h"
#include "Function/Render/RenderPass/VolumetricFogPass.h"
#include "Function/Render/RenderPass/ReSTIRDIPass.h"
#include "Function/Render/RenderPass/ReSTIRGIPass.h"
#include "Function/Render/RenderPass/SVGFPass.h"
#include "Function/Render/RenderPass/NRDPass.h"
#include "Function/Render/RenderPass/ForwardPass.h"
#include "Function/Render/RenderPass/ClipmapVisualizePass.h"
#include "Function/Render/RenderPass/DDGIVisualizePass.h"
#include "Function/Render/RenderPass/BloomPass.h"
#include "Function/Render/RenderPass/FXAAPass.h"
#include "Function/Render/RenderPass/TAAPass.h"
#include "Function/Render/RenderPass/ExposurePass.h"
#include "Function/Render/RenderPass/PostProcessingPass.h"
#include "Function/Render/RenderPass/RayTracingBasePass.h"
#include "Function/Render/RenderPass/PathTracingPass.h"
#include "Function/Render/RenderPass/GizmoPass.h"
#include "Function/Render/RenderPass/EditorUIPass.h"
#include "Function/Render/RenderPass/TestTrianglePass.h"
#include "Function/Render/RenderPass/PresentPass.h"
#include "Function/Render/RenderPass/RenderPass.h"
#include "Platform/HAL/PlatformProcess.h"
#include "RenderSurfaceCacheManager.h"
#include <cstdio>
#include <memory>

void RenderSystem::InitGLFW()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(WINDOW_EXTENT.width, WINDOW_EXTENT.height, "Toy Renderer", nullptr, nullptr); 
    // glfwSetWindowUserPointer(m_window, this);
    // glfwSetWindowSizeCallback(m_window, nullptr); //TODO
    // glfwSetCursorPosCallback(m_window, MousePosCallback);
    // glfwSetMouseButtonCallback(m_window, MouseButtonCallback);
    // glfwSetScrollCallback(m_window, ScrollCallback);
    // glfwSetKeyCallback(m_window, KeyCallback);
}

void RenderSystem::DestroyGLFW()
{
    glfwDestroyWindow(window);
    glfwTerminate();
}

void RenderSystem::Init() 
{ 
    EngineContext::RHI()->InitImGui(window);

    lightManager = std::make_shared<RenderLightManager>();
    meshManager = std::make_shared<RenderMeshManager>();
    surfaceCacheManager = std::make_shared<RenderSurfaceCacheManager>();
    lightManager->Init();
    meshManager->Init();
    surfaceCacheManager->Init();

    InitBaseResource(); 
    InitPasses();  
}

void RenderSystem::InitBaseResource()
{
    backend       = EngineContext::RHI();
    surface       = backend->CreateSurface(window);
    queue         = backend->GetQueue({ QUEUE_TYPE_GRAPHICS, 0 });
    swapchain     = backend->CreateSwapChain({ surface, queue, FRAMES_IN_FLIGHT, surface->GetExetent(), COLOR_FORMAT });
    pool          = backend->CreateCommandPool({ queue });  
    for(uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) 
    {
        perFrameCommonResources[i].command = pool->CreateCommandList(false);
        perFrameCommonResources[i].startSemaphore = backend->CreateSemaphore();
        perFrameCommonResources[i].finishSemaphore = backend->CreateSemaphore();
        perFrameCommonResources[i].fence = backend->CreateFence(true);
    }
}

void RenderSystem::InitPasses()
{
    meshPasses[MESH_DEPTH_PASS]                 = std::make_shared<DepthPass>();
    meshPasses[MESH_DIRECTIONAL_SHADOW_PASS]    = std::make_shared<DirectionalShadowPass>();
    meshPasses[MESH_POINT_SHADOW_PASS]          = std::make_shared<PointShadowPass>();
    meshPasses[MESH_G_BUFFER_PASS]              = std::make_shared<GBufferPass>();
    meshPasses[MESH_FORWARD_PASS]               = std::make_shared<ForwardPass>();

    passes[GPU_CULLING_PASS]                    = std::make_shared<GPUCullingPass>();
    passes[CLUSTER_LIGHTING_PASS]               = std::make_shared<ClusterLightingPass>();
    passes[IBL_PASS]                            = std::make_shared<IBLPass>();
    passes[DEPTH_PASS]                          = meshPasses[MESH_DEPTH_PASS];
    passes[DEPTH_PYRAMID_PASS]                  = std::make_shared<DepthPyramidPass>();         // 0.3
    passes[POINT_SHADOW_PASS]                   = meshPasses[MESH_POINT_SHADOW_PASS];
    passes[DIRECTIONAL_SHADOW_PASS]             = meshPasses[MESH_DIRECTIONAL_SHADOW_PASS];
    passes[CLIPMAP_PASS]                        = nullptr;
    passes[DDGI_PASS]                           = nullptr;
    passes[G_BUFFER_PASS]                       = meshPasses[MESH_G_BUFFER_PASS];
    passes[SURFACE_CACHE_PASS]                  = nullptr;
    passes[REPROJECTION_PASS]                   = std::make_shared<ReprojectionPass>();
    passes[DEFERRED_LIGHTING_PASS]              = std::make_shared<DeferredLightingPass>();     // 0 ?
    passes[RESTIR_DI_PASS]                      = nullptr;
    passes[RESTIR_GI_PASS]                      = nullptr;
    passes[SVGF_PASS]                           = nullptr;
    passes[SSSR_PASS]                           = std::make_shared<SSSRPass>();
    passes[NRD_PASS]                            = nullptr;
    passes[VOLUMETIRC_FOG_PASS]                 = std::make_shared<VolumetricFogPass>();
    passes[FORWARD_PASS]                        = meshPasses[MESH_FORWARD_PASS];
    passes[CLIPMAP_VISUALIZE_PASS]              = nullptr;
    passes[DDGI_VISUALIZE_PASS]                 = nullptr;
    passes[TRANSPARENT_PASS]                    = nullptr;
    passes[PATH_TRACING_PASS]                   = nullptr;
    passes[RAY_TRACING_BASE_PASS]               = nullptr;
    passes[BLOOM_PASS]                          = std::make_shared<BloomPass>();
    passes[FXAA_PASS]                           = std::make_shared<FXAAPass>();
    passes[TAA_PASS]                            = std::make_shared<TAAPass>();
    passes[EXPOSURE_PASS]                       = std::make_shared<ExposurePass>();
    passes[POST_PROCESSING_PASS]                = std::make_shared<PostProcessingPass>();
    passes[GIZMO_PASS]                          = std::make_shared<GizmoPass>();             // 0.4
    passes[TEST_TRIANGLE_PASS]                  = std::make_shared<TestTrianglePass>();
    passes[EDITOR_UI_PASS]                      = std::make_shared<EditorUIPass>();
    passes[PRESENT_PASS]                        = std::make_shared<PresentPass>();

    passes[BLOOM_PASS]->SetEnable(false);       //TODO 
    passes[VOLUMETIRC_FOG_PASS]->SetEnable(false);

#if ENABLE_RAY_TRACING
    passes[CLIPMAP_PASS]                    = std::make_shared<ClipmapPass>(); 
    passes[DDGI_PASS]                       = std::make_shared<DDGIPass>();  
    passes[SURFACE_CACHE_PASS]              = std::make_shared<SurfaceCachePass>();         //
    passes[RESTIR_DI_PASS]                  = std::make_shared<ReSTIRDIPass>();
    passes[RESTIR_GI_PASS]                  = std::make_shared<ReSTIRGIPass>();             // 0.6
    passes[SVGF_PASS]                       = std::make_shared<SVGFPass>();
    passes[NRD_PASS]                        = std::make_shared<NRDPass>();                  // 0.7
    passes[CLIPMAP_VISUALIZE_PASS]          = std::make_shared<ClipmapVisualizePass>();
    passes[DDGI_VISUALIZE_PASS]             = std::make_shared<DDGIVisualizePass>();
    passes[PATH_TRACING_PASS]               = std::make_shared<PathTracingPass>();
    passes[RAY_TRACING_BASE_PASS]           = std::make_shared<RayTracingBasePass>();

    passes[CLIPMAP_PASS]->SetEnable(false);
    passes[CLIPMAP_VISUALIZE_PASS]->SetEnable(false);
    passes[RESTIR_DI_PASS]->SetEnable(false);
    passes[SVGF_PASS]->SetEnable(false);
    passes[PATH_TRACING_PASS]->SetEnable(false);  
    passes[RAY_TRACING_BASE_PASS]->SetEnable(false);      
#endif

    for(auto& pass : passes) { if(pass) pass->Init(); }
}

bool RenderSystem::Tick()
{
    ENGINE_TIME_SCOPE(RenderSystem::Tick);
    if(!glfwWindowShouldClose(window))
    {
        if(EngineContext::World()->GetActiveScene() == nullptr) return false;

        {
            ENGINE_TIME_SCOPE(RenderSystem::WaitFence);
            auto& resource = perFrameCommonResources[EngineContext::ThreadPool()->ThreadFrameIndex()];
            resource.fence->Wait();                         // 等待帧栅栏，前一次本帧执行完毕后本帧才可重新开始收集和提交数据
        }    
        {
            ENGINE_TIME_SCOPE(RenderSystem::TickManagers);
            // meshManager->Tick();             // 先准备各个meshpass的绘制信息
            // lightManager->Tick();            // 准备光源信息   
            // surfaceCacheManager->Tick();     // 更新surfaceCache
            // UpdateGlobalSetting(); 
            
            // 非常简单的并行  
            EngineContext::ThreadPool()->AddQueuedWork([this](){
                surfaceCacheManager->Tick();
            });
            EngineContext::ThreadPool()->AddQueuedWork([this](){
                meshManager->Tick();    
            });
            EngineContext::ThreadPool()->AddQueuedWork([this](){
                lightManager->Tick();
            });
            EngineContext::ThreadPool()->AddQueuedWork([this](){
                UpdateGlobalSetting();   
            });
            EngineContext::ThreadPool()->WaitIdle();
        }
        
        BuildRDG(); // RDG的构建目前暂未支持多线程并行，只能串行；执行需要依赖于上面几个manager的数据处理结果
                    // 上面的WaitIdle该做成task graph的执行依赖
        ExecuteRDG();                             
        
        {
            ENGINE_TIME_SCOPE(RenderSystem::SyncRHI);                                   // GPU端瓶颈会导致此处的WaitIdle等待
            EngineContext::ThreadPool()->WaitIdle(ENGINE_THREAD_TYPE_RHI);  // loop里唯一和RHI线程同步的时点，RHI最多会延迟主线程一帧
            EngineContext::ThreadPool()->AddQueuedWork([this](){    
                meshManager->UpdateTLAS();  // TODO vk等支持多线程的指令录制，但是不支持同queue并行提交，此处的TLAS更新里有一个提交，不能和上面的前一帧指令并行，需要再改          
                SubmitRHI();               
                                            
            }, ENGINE_THREAD_TYPE_RHI);  
        }       

        return false; 
    }
    return true;
}

void RenderSystem::BuildRDG()
{
    auto& resource = perFrameCommonResources[EngineContext::ThreadPool()->ThreadFrameIndex()];
    auto& rdgBuilder = rdgBuilders[EngineContext::ThreadPool()->ThreadFrameIndex()];

    RHICommandListRef command = resource.command;   // 构建RDG，绘制提交
    command->BeginCommand();
    rdgBuilder = std::make_shared<RDGBuilder>(command);
    {
        ENGINE_TIME_SCOPE(RenderSystem::RDGBuild);

        if(passes[TEST_TRIANGLE_PASS]->IsEnabled()) // Test
        {
            passes[TEST_TRIANGLE_PASS]->Build(*rdgBuilder.get()); 
            passes[EDITOR_UI_PASS]->Build(*rdgBuilder.get()); 
            passes[PRESENT_PASS]->Build(*rdgBuilder.get()); 
        }
        else {
            for(auto& pass : passes) 
            { 
                if(pass) 
                {
                    ENGINE_TIME_SCOPE_STR("RDGBuilder::BuildPass::" + pass->GetName());
                    pass->Build(*rdgBuilder.get()); 
                }
            }
        }
    }
}

void RenderSystem::ExecuteRDG()
{
    ENGINE_TIME_SCOPE(RenderSystem::RDGExecute);

    auto& rdgBuilder = rdgBuilders[EngineContext::ThreadPool()->ThreadFrameIndex()];
    if(rdgBuilder)
    {
        rdgBuilder->Execute();
        rdgDependencyGraph = rdgBuilder->GetGraph(); //
    }
}

void RenderSystem::SubmitRHI()
{   
    ENGINE_TIME_SCOPE(RenderSystem::RecordCommands);

    auto& resource = perFrameCommonResources[EngineContext::ThreadPool()->ThreadFrameIndex()];
    RHITextureRef swapchainTexture = swapchain->GetNewFrame(nullptr, resource.startSemaphore);
    RHICommandListRef command = resource.command; 
    command->EndCommand();
    command->Execute(resource.fence, resource.startSemaphore, resource.finishSemaphore);    // 指令提交
    swapchain->Present(resource.finishSemaphore); 
}

void RenderSystem::UpdateGlobalSetting()
{
    globalSetting.totalTicks = EngineContext::GetCurretTick();
    globalSetting.totalTickTime += EngineContext::GetDeltaTime();
    globalSetting.skyboxMaterialID = EngineContext::World()->GetActiveScene()->GetSkyBox() ?
                                     EngineContext::World()->GetActiveScene()->GetSkyBox()->GetMaterialID() : 0;
    //globalSetting.clusterInspectMode;   // 在Editor里设置
    EngineContext::RenderResource()->SetRenderGlobalSetting(globalSetting);
}

