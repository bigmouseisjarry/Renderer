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

void RenderSystem::InitSDL()
{
    // TODO:后续加入SDL_INIT_AUDIO
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("Toy Renderer", WINDOW_EXTENT.width, WINDOW_EXTENT.height, SDL_WINDOW_VULKAN);
}

void RenderSystem::DestroySDL()
{
    SDL_DestroyWindow(window);
    SDL_Quit();
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
        perFrameCommonResources[i].GraphicsCommand = pool->CreateCommandList(false);
        perFrameCommonResources[i].startSemaphore = backend->CreateSemaphore();
        perFrameCommonResources[i].finishSemaphore = backend->CreateSemaphore();
        perFrameCommonResources[i].fence = backend->CreateFence(true);

        rdgCompilers[i] = std::make_shared<RDGCompiler>();
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

void RenderSystem::Tick()
{
    ENGINE_TIME_SCOPE(RenderSystem::Tick);
    if (EngineContext::World()->GetActiveScene() == nullptr) return;

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
        EngineContext::ThreadPool()->AddQueuedWork([this]() {
            surfaceCacheManager->Tick();
            });
        EngineContext::ThreadPool()->AddQueuedWork([this]() {
            meshManager->Tick();
            });
        EngineContext::ThreadPool()->AddQueuedWork([this]() {
            lightManager->Tick();
            });
        EngineContext::ThreadPool()->AddQueuedWork([this]() {
            UpdateGlobalSetting();
            });
        EngineContext::ThreadPool()->WaitIdle();
    }

    BuildRDG(); // RDG的构建目前暂未支持多线程并行，只能串行；执行需要依赖于上面几个manager的数据处理结果
   
    // TODO:添加图分析
    RDGCompilerRef rdgCompiler = rdgCompilers[EngineContext::ThreadPool()->ThreadFrameIndex()];
    rdgCompiler->compile_and_execute(
        rdgDependencyGraph,
        &perFrameCommonResources[EngineContext::ThreadPool()->ThreadFrameIndex()]
    );

    // 验证 PassInfoAnalysis 结果
    {
        const auto& infoAnalysis = rdgCompiler->GetPassInfoAnalysis();
        ENGINE_LOG_INFO("=== PassInfoAnalysis Result ===");
        ENGINE_LOG_INFO("Pass count: {}", rdgDependencyGraph->PassNodeCount());
        ENGINE_LOG_INFO("Resource count: {}", rdgDependencyGraph->ResourceNodeCount());

        // 按类型汇总
        uint32_t renderCount = 0, computeCount = 0, copyCount = 0, presentCount = 0, rtCount = 0;
        uint32_t totalResourcesAccessed = 0;
        uint32_t hintAsyncCompute = 0, hintSeparateCmdBuf = 0;
        rdgDependencyGraph->ForEachPassNode([&](RDGPassNodeRef pass) {
            const auto* passInfo = infoAnalysis.get_pass_info(pass);
            if (!passInfo) return;
            switch (passInfo->pass_type) {
                case RDGPassNodeType::RDG_PASS_NODE_TYPE_RENDER:      renderCount++; break;
                case RDGPassNodeType::RDG_PASS_NODE_TYPE_COMPUTE:     computeCount++; break;
                case RDGPassNodeType::RDG_PASS_NODE_TYPE_COPY:        copyCount++; break;
                case RDGPassNodeType::RDG_PASS_NODE_TYPE_PRESENT:     presentCount++; break;
                case RDGPassNodeType::RDG_PASS_NODE_TYPE_RAY_TRACING: rtCount++; break;
                default: break;
            }
            totalResourcesAccessed += passInfo->resource_info.total_resource_count;
            if (passInfo->performance_info.prefers_async_compute) hintAsyncCompute++;
            if (passInfo->performance_info.separate_command_buffer) hintSeparateCmdBuf++;
        });
        ENGINE_LOG_INFO("Pass summary: Render={} Compute={} Copy={} Present={} RayTracing={}",
            renderCount, computeCount, copyCount, presentCount, rtCount);
        ENGINE_LOG_INFO("Total resource accesses: {}", totalResourcesAccessed);
        ENGINE_LOG_INFO("Performance hints: asyncCompute={} separateCmdBuf={}", hintAsyncCompute, hintSeparateCmdBuf);

        // 逐 pass 输出（只输出 pass 级，不展开每个资源）
        rdgDependencyGraph->ForEachPassNode([&](RDGPassNodeRef pass) {
            const auto* passInfo = infoAnalysis.get_pass_info(pass);
            if (!passInfo) { ENGINE_LOG_INFO("  [PASS] {} - no info", pass->Name()); return; }

            const char* typeStr = "Unknown";
            switch (passInfo->pass_type) {
                case RDGPassNodeType::RDG_PASS_NODE_TYPE_RENDER:      typeStr = "R"; break;
                case RDGPassNodeType::RDG_PASS_NODE_TYPE_COMPUTE:     typeStr = "C"; break;
                case RDGPassNodeType::RDG_PASS_NODE_TYPE_RAY_TRACING: typeStr = "RT"; break;
                case RDGPassNodeType::RDG_PASS_NODE_TYPE_PRESENT:     typeStr = "P"; break;
                case RDGPassNodeType::RDG_PASS_NODE_TYPE_COPY:        typeStr = "CP"; break;
                default: break;
            }

            // 统计该 pass 的读写资源数
            uint32_t texCount = 0, bufCount = 0, writeCount = 0, readCount = 0;
            for (const auto& access : passInfo->resource_info.resource_accesses)
            {
                if (access.resource->NodeType() == RDG_RESOURCE_NODE_TYPE_TEXTURE) texCount++;
                else bufCount++;
                if (access.access_type == EResourceAccessType::ReadWrite) { readCount++; writeCount++; }
                else if (access.access_type == EResourceAccessType::Write)       writeCount++;
                else                                                             readCount++;
            }
            ENGINE_LOG_INFO("  [{}] {} tex={} buf={} reads={} writes={}",
                typeStr, pass->Name(), texCount, bufCount, readCount, writeCount);
        });

        // 资源跨 pass 汇总
        uint32_t crossQueueResources = 0;
        rdgDependencyGraph->ForEachTextureNode([&](RDGTextureNodeRef tex) {
            const auto* resInfo = infoAnalysis.get_resource_info((RDGResourceNodeRef)tex);
            if (resInfo) {
                // queues 位掩码: 0x01=Graphics, 0x02=Compute, 0x04=Transfer
                bool isCross = resInfo->access_queues != 0 && (resInfo->access_queues & (resInfo->access_queues - 1)) != 0;
                if (isCross) crossQueueResources++;
            }
        });
        rdgDependencyGraph->ForEachBufferNode([&](RDGBufferNodeRef buf) {
            const auto* resInfo = infoAnalysis.get_resource_info((RDGResourceNodeRef)buf);
            if (resInfo) {
                bool isCross = resInfo->access_queues != 0 && (resInfo->access_queues & (resInfo->access_queues - 1)) != 0;
                if (isCross) crossQueueResources++;
            }
        });
        ENGINE_LOG_INFO("Cross-queue resources: {}", crossQueueResources);
        ENGINE_LOG_INFO("=== End PassInfoAnalysis Result ===");
    }

    // 根据分析结果分别录制+执行，而不是现在这样串行一个队列执行
    ExecuteRDG();

    {
        ENGINE_TIME_SCOPE(RenderSystem::SyncRHI);                                   // GPU端瓶颈会导致此处的WaitIdle等待
        EngineContext::ThreadPool()->WaitIdle(ENGINE_THREAD_TYPE_RHI);  // loop里唯一和RHI线程同步的时点，RHI最多会延迟主线程一帧
        EngineContext::ThreadPool()->AddQueuedWork([this]() {
            meshManager->UpdateTLAS();  // TODO vk等支持多线程的指令录制，但是不支持同queue并行提交，此处的TLAS更新里有一个提交，不能和上面的前一帧指令并行，需要再改          
            SubmitRHI();

            }, ENGINE_THREAD_TYPE_RHI);
    }
}

void RenderSystem::BuildRDG()
{
    auto& resource = perFrameCommonResources[EngineContext::ThreadPool()->ThreadFrameIndex()];
    auto& rdgBuilder = rdgBuilders[EngineContext::ThreadPool()->ThreadFrameIndex()];

    RHICommandListRef command = resource.GraphicsCommand;   // 构建RDG，绘制提交

    // 现在是cmd自己每帧做reset，改成pre-frame整体pool做reset
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

    rdgDependencyGraph = rdgBuilder->GetGraph();
}

void RenderSystem::ExecuteRDG()
{
    ENGINE_TIME_SCOPE(RenderSystem::RDGExecute);

    auto& rdgBuilder = rdgBuilders[EngineContext::ThreadPool()->ThreadFrameIndex()];
    if(rdgBuilder)
    {
        rdgBuilder->Execute();
        // rdgDependencyGraph = rdgBuilder->GetGraph();
    }
}

void RenderSystem::SubmitRHI()
{   
    ENGINE_TIME_SCOPE(RenderSystem::RecordCommands);

    auto& resource = perFrameCommonResources[EngineContext::ThreadPool()->ThreadFrameIndex()];
    RHITextureRef swapchainTexture = swapchain->GetNewFrame(nullptr, resource.startSemaphore);
    RHICommandListRef command = resource.GraphicsCommand; 
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
