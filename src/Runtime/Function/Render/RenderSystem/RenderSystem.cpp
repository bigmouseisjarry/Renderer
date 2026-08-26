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
#include "Function/Render/RenderPass/TLASUpdatePass.h"
#include "Function/Render/RenderPass/PresentPass.h"
#include "Function/Render/RenderPass/RenderPass.h"
#include "RenderSurfaceCacheManager.h"
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
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
       // perFrameCommonResources[i].GraphicsCommand = pool->CreateCommandList(false);
        perFrameCommonResources[i].startSemaphore = backend->CreateSemaphore();
        perFrameCommonResources[i].finishSemaphore = backend->CreateSemaphore();
        perFrameCommonResources[i].fence = backend->CreateFence(true);


        QueueScheduleConfig queueCfg{};
        queueCfg.enable_async_compute = true;
        queueCfg.max_async_compute_queues = 1;
        queueCfg.max_copy_queues = 1;
        queueCfg.enable_copy_queue = true;
        queueCfg.enable_debug_output = false;

        ExecutionReorderConfig reorderConfig{};
        reorderConfig.enable_cache_opt = true;
        reorderConfig.enable_lifetime_opt = false;
        reorderConfig.max_attraction_distance = 10;
        reorderConfig.min_affinity_score = 0.3;

        CrossQueueSyncConfig crossQueueSyncConfig{};
        crossQueueSyncConfig.enable_ssis_optimization = true;
        crossQueueSyncConfig.enable_debug_output = false;
        crossQueueSyncConfig.max_sync_distance = 16;

        PassBindingConfig passBindingConfig{};
        passBindingConfig.enable_debug_output = false;

        BarrierGenerationConfig barrierGenerationConfig{};
        barrierGenerationConfig.enable_debug_output = false;

        CommandRecordingConfig commandRecordingConfig{};
        commandRecordingConfig.enable_debug_markers = true;
        commandRecordingConfig.enable_debug_output = false;
        // commandRecordingConfig.enable_parallel_recording = useParallelRecording;
        commandRecordingConfig.chunk_count = 3;     // ANY池worker(2) + 主线程(1)
        rdgCompilers[i] = std::make_shared<RDGCompiler>(queueCfg, reorderConfig, crossQueueSyncConfig, passBindingConfig, barrierGenerationConfig, commandRecordingConfig);
    }
}

void RenderSystem::EnsureFrameChunkLists(PerFrameCommonResource& resource, uint32_t count)
{
    // 惰性增长到所需chunk数；列表按帧槽持久持有（context独占，跨帧由帧槽fence保证复用安全，
    // 每帧由各chunk的BeginCommand重置）。主线程调用，无并发分配。
    // 注意：每个chunk使用独立的命令池——vkBegin/vkEnd/vkResetCommandBuffer要求父VkCommandPool
    // 外部同步，多worker共享一个池并发BeginCommand是未定义行为（驱动访问冲突）
    while (static_cast<uint32_t>(resource.ChunkCommands.size()) < count)
    {
        RHICommandPoolRef chunkPool = backend->CreateCommandPool({ queue });
        chunkPools.push_back(chunkPool);                                   // 显式持有池生命周期
        resource.ChunkCommands.push_back(chunkPool->CreateCommandList(true));   // byPass=true：立即录制
    }
}

void RenderSystem::InitPasses()
{
    meshPasses[MESH_DEPTH_PASS]                 = std::make_shared<DepthPass>();
    meshPasses[MESH_DIRECTIONAL_SHADOW_PASS]    = std::make_shared<DirectionalShadowPass>();
    meshPasses[MESH_POINT_SHADOW_PASS]          = std::make_shared<PointShadowPass>();
    meshPasses[MESH_G_BUFFER_PASS]              = std::make_shared<GBufferPass>();
    meshPasses[MESH_FORWARD_PASS]               = std::make_shared<ForwardPass>();

    passes[TLAS_UPDATE_PASS]                    = std::make_shared<TLASUpdatePass>();
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
    passes[RESTIR_DI_PASS]->SetEnable(false);
    passes[SVGF_PASS]->SetEnable(false);
    passes[CLIPMAP_VISUALIZE_PASS]->SetEnable(false);
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

    // 根据分析结果分别录制+执行，而不是现在这样串行一个队列执行
    // 分别录制已完成
    ExecuteRDG();

    {
        ENGINE_TIME_SCOPE(RenderSystem::SyncRHI);                                   // GPU端瓶颈会导致此处的WaitIdle等待
        EngineContext::ThreadPool()->WaitIdle(ENGINE_THREAD_TYPE_RHI);  // loop里唯一和RHI线程同步的时点，RHI最多会延迟主线程一帧
        EngineContext::ThreadPool()->AddQueuedWork([this]() {
            SubmitRHI();    // TLAS更新已收编进RDG（TLASUpdatePass在帧命令流内录制构建，不再有独立提交与等待）
            }, ENGINE_THREAD_TYPE_RHI);
    }
}

void RenderSystem::BuildRDG()
{
    auto& resource = perFrameCommonResources[EngineContext::ThreadPool()->ThreadFrameIndex()];
    auto& rdgBuilder = rdgBuilders[EngineContext::ThreadPool()->ThreadFrameIndex()];

    rdgBuilder = std::make_shared<RDGBuilder>();
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

    RDGCompilerRef rdgCompiler = rdgCompilers[EngineContext::ThreadPool()->ThreadFrameIndex()];
    auto& frameResource = perFrameCommonResources[EngineContext::ThreadPool()->ThreadFrameIndex()];
    frameResource.builder = rdgBuilders[EngineContext::ThreadPool()->ThreadFrameIndex()].get();   // PassExecutionPhase 组装RDGPassContext用

    // chunked并行录制：帧首fence已保证本帧槽上一轮的chunk命令缓冲执行完毕（可安全BeginCommand重置）
    frameResource.chunkCount = 3;
    EnsureFrameChunkLists(frameResource, 3);

    rdgCompiler->compile_and_execute(
        rdgDependencyGraph,
        &frameResource
    );
}

void RenderSystem::SubmitRHI()
{
    ENGINE_TIME_SCOPE(RenderSystem::RecordCommands);

    auto& resource = perFrameCommonResources[EngineContext::ThreadPool()->ThreadFrameIndex()];
    RHITextureRef swapchainTexture = swapchain->GetNewFrame(nullptr, resource.startSemaphore);
    assert(resource.chunkCount > 0);
    

    // 并行录制：各chunk已在录制线程各自Begin/End，此处按chunk序单次批量提交
    // （一次vkQueueSubmit内多个primary buffer按序执行，语义等价于单buffer串接）
    std::vector<RHICommandListRef> chunks(resource.ChunkCommands.begin(),
                                            resource.ChunkCommands.begin() + resource.chunkCount);
    RHICommandList::ExecuteBatch(chunks, resource.fence, resource.startSemaphore, resource.finishSemaphore);

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
