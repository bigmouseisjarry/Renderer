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
#include <memory>

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
    context->renderSystem->InitGLFW();

    context->inputSystem = std::make_shared<InputSystem>();
    context->inputSystem->Init();
    context->inputSystem->InitGLFW();

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
    while (!exit) 
    {
        UpdateTimers();
        eventSystem->Tick();

        ENGINE_TIME_SCOPE(EngineContext::MainLoopInternal);
        {
            ENGINE_TIME_SCOPE(EngineContext::SystemTick);

            EngineContext::ThreadPool()->AddQueuedWork([this](){
                worldManager->Tick(deltaTime);
            });
            EngineContext::ThreadPool()->AddQueuedWork([this](){
                assetManager->Tick();
            });
            EngineContext::ThreadPool()->AddQueuedWork([this](){
                rhiBackend->Tick();
            }, ENGINE_THREAD_TYPE_RHI);

            inputSystem->Tick();  // glfw多线程？
            
            EngineContext::ThreadPool()->WaitIdle();   
        }
        {
            ENGINE_TIME_SCOPE(EngineContext::RenderTick);
            exit = renderSystem->Tick();
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
    renderSystem->DestroyGLFW();
}

void EngineContext::UpdateTimers()
{
    historyTimers = timers[currentTick % (2 * FRAMES_IN_FLIGHT)];

    for(auto& timerPair : timers[currentTick % (2 * FRAMES_IN_FLIGHT)])    // 计时需要在全部同步之后做更新
        timerPair.second = std::make_shared<TimeScopes>(); // 重新生成对象，不clear了
    
    timer.EndAfterMilliSeconds(renderSystem->GetGlobalSetting()->minFrameTime);
    deltaTime = timer.GetMilliSeconds();
    timer.Clear();
    timer.Begin();  
}