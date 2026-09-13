#include "EngineContext.h"
#include "Core/Event/EventSystem.h"
#include "Core/Log/LogSystem.h"
#include "Core/Math/Math.h"
#include "Core/Util/TimeScope.h"
#include "Eigen/src/Core/products/Parallelizer.h"
#include "EngineThreadPool.h"
#include "Function/Framework/World/WorldManager.h"
#include "Function/Global/Definations.h"
#include "Function/Render/RHI/RHI.h"
#include "Function/Render/RenderResource/RenderResourceManager.h"
#include "Platform/File/FileSystem.h"
#include "Platform/HAL/PlatformProcess.h"
#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

std::shared_ptr<EngineContext> EngineContext::context = std::make_shared<EngineContext>();

std::shared_ptr<EngineContext> EngineContext::Init()
{  
    context->eventSystem = std::make_shared<EventSystem>();
    context->eventSystem->Init();

    context->logSystem = std::make_shared<LogSystem>();
    context->logSystem->Init();

    context->threadPool = std::make_shared<EngineThreadPool>();
    context->threadPool->Init();

    context->fileSystem = std::make_shared<FileSystem>();
    context->fileSystem->Init("renderer");

    context->renderSystem = std::make_shared<RenderSystem>();
    context->renderSystem->InitSDL();

	context->inputSystem = std::make_shared<InputSystem>();
    
    context->rhiBackend = RHIBackend::Init({.type = BACKEND_VULKAN, .enableDebug = true, .enableRayTracing = ENABLE_RAY_TRACING});

    context->renderResourceManger = std::make_shared<RenderResourceManager>();
    context->renderResourceManger->Init();

    context->renderSystem->Init();

    context->editorSystem = std::make_shared<EditorSystem>();
    context->editorSystem->Init();

    context->worldManager = std::make_shared<WorldManager>(); 
    //context->worldManager->Init("resource/build_in/config/scene/default.scene");

    context->assetManager = std::make_shared<AssetManager>();
    context->assetManager->Init();

    return context;
}

void EngineContext::MainLoopInternal()
{
    bool exit = false;
    for(;;)
    {
        UpdateTimers();
        eventSystem->Tick();

        ENGINE_TIME_SCOPE(EngineContext::MainLoopInternal);
        {
            ENGINE_TIME_SCOPE(EngineContext::SystemTick);

            // 还可以使用输入状态双缓冲来解决输入竞态问题
            exit = inputSystem->Tick();
            if (exit) break;

            EngineContext::ThreadPool()->AddQueuedWork([this](){
                worldManager->Tick(deltaTime);
            });
            EngineContext::ThreadPool()->AddQueuedWork([this](){
                assetManager->Tick();
            });
            EngineContext::ThreadPool()->AddQueuedWork([this](){
                rhiBackend->Tick();
            }, ENGINE_THREAD_TYPE_RHI);

            EngineContext::ThreadPool()->WaitIdle();   
        }
        {
            ENGINE_TIME_SCOPE(EngineContext::RenderTick);
            // exit = renderSystem->Tick();
            renderSystem->Tick();
        }

        currentFrameIndex = (currentFrameIndex + 1) % FRAMES_IN_FLIGHT;
        currentTick++;
    }
}

void EngineContext::DestroyInternal()
{
    ENGINE_LOG_INFO("Engine context destructed.");
    renderResourceManger->Destroy();
    renderSystem->Destroy();
    //worldManager->Save();
    //assetManager->Save();
    rhiBackend->Destroy();
    threadPool->Destroy();
    logSystem->Destroy();
    eventSystem->Destroy();
	renderSystem->DestroySDL();
}

void EngineContext::UpdateTimers()
{
    historyTimers = timers[currentTick % (2 * FRAMES_IN_FLIGHT)];

    // [CpuTiming]节流聚合导出（CPU帧用时分析，风格同RHIRenderQuery的[GpuTiming]）：按scope名跨帧
    // 聚合均值/最大值/每帧命中数，每300帧输出一批（含全部ENGINE_TIME_SCOPE/ENGINE_TIME_SCOPE_STR
    // 函数——per-pass动态名亦在其中）。数据源=historyTimers（上一帧全部线程的scope集，worker任务
    // 在帧内WaitIdle join后闭合）；唯RHI线程的SubmitRHI在Tick返回后仍可能在录——Valid()==false
    //（有scope未Pop）的线程该帧跳过，避免读到半开区间
    static uint32_t dumpCounter = 0;
    static uint32_t dumpFrames = 0;
    static double frameMsSum = 0.0;
    static double loopMsSum = 0.0;
    static std::map<std::string, std::array<double, 3>> cpuTimingAgg;   // name → {sumMs, maxMs, hits}
    constexpr uint32_t CPU_TIMING_DUMP_INTERVAL = 300;

    dumpFrames++;
    frameMsSum += deltaTime;
    for (auto& [threadID, scopes] : historyTimers)
    {
        if (!scopes || !scopes->Valid()) continue;
        for (const auto& scope : scopes->GetScopes())
        {
            const double ms = scope->GetMilliSeconds();
            auto& agg = cpuTimingAgg[scope->name];
            agg[0] += ms;
            agg[1] = std::max(agg[1], ms);
            agg[2] += 1.0;
            if (scope->name == "EngineContext::MainLoopInternal") loopMsSum += ms;
        }
    }

    if (++dumpCounter >= CPU_TIMING_DUMP_INTERVAL)
    {
        dumpCounter = 0;
        std::vector<std::pair<std::string, std::array<double, 3>>> sorted(cpuTimingAgg.begin(), cpuTimingAgg.end());
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second[0] > b.second[0]; });
        //ENGINE_LOG_INFO("[CpuTiming] ===== dump: frames={} frameAvg={:.2f}ms loopAvg={:.2f}ms names={} =====",
        //    dumpFrames, frameMsSum / dumpFrames, loopMsSum / dumpFrames, sorted.size());
        for (const auto& [name, agg] : sorted)
            //ENGINE_LOG_INFO("[CpuTiming] '{}' avg={:.3f}ms max={:.2f}ms hits={:.1f}/frame",
            //    name.c_str(), agg[0] / dumpFrames, agg[1], agg[2] / dumpFrames);

        cpuTimingAgg.clear();
        dumpFrames = 0;
        frameMsSum = 0.0;
        loopMsSum = 0.0;
    }

    for(auto& timerPair : timers[currentTick % (2 * FRAMES_IN_FLIGHT)])    // 计时需要在全部同步之后做更新
        timerPair.second = std::make_shared<TimeScopes>(); // 重新生成对象，不clear了

    timer.EndAfterMilliSeconds(renderSystem->GetGlobalSetting()->minFrameTime);
    deltaTime = timer.GetMilliSeconds();
    timer.Clear();
    timer.Begin();
}