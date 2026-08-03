#include "EngineThreadPool.h"
#include "Function/Global/EngineContext.h"
#include "Platform/HAL/PlatformProcess.h"
#include <cstdint>

thread_local uint32_t EngineThreadPool::threadFrameIndex = 0;
thread_local uint32_t EngineThreadPool::threadTick = 0;

void EngineThreadPool::Init()
{
    mainThreadID = PlatformProcess::GetThreadID();

    rhiThread = QueuedThreadPool::Create(1);
    anyThread = QueuedThreadPool::Create(2);
}

uint32_t EngineThreadPool::ThreadFrameIndex()
{
    uint32_t currentThreadID = PlatformProcess::GetThreadID();
    if(currentThreadID == mainThreadID) return EngineContext::CurrentFrameIndex();
    return threadFrameIndex;
}

uint32_t EngineThreadPool::ThreadPreviousFrameIndex()
{
    return (ThreadFrameIndex() + FRAMES_IN_FLIGHT - 1) % FRAMES_IN_FLIGHT;
}

uint32_t EngineThreadPool::ThreadTick()
{
    uint32_t currentThreadID = PlatformProcess::GetThreadID();
    if(currentThreadID == mainThreadID) return EngineContext::GetCurretTick();
    return threadTick;
}

void EngineThreadPool::WaitIdle(EngineThreadType threadType)
{
    auto thread = TypeToThreadPool(threadType);
    if(thread)
        thread->WaitIdle();
}

void EngineThreadPool::WaitAllIdle()
{
    rhiThread->WaitIdle();
    anyThread->WaitIdle();
}

void EngineThreadPool::Destroy()
{
    rhiThread->Destroy();
    anyThread->Destroy();
}

void EngineThreadPool::AddQueuedWork(QueuedWorkFunc func, EngineThreadType threadType)
{
    uint32_t frameIndex = ThreadFrameIndex();               // 线程执行前，将帧设置为录制该指令时对应线程的对应时间
    uint32_t tick = ThreadTick();
    auto lambda = [frameIndex, tick, func](){
        threadFrameIndex = frameIndex;  
        threadTick = tick; 
        func();
    };

    // lambda();    // 不使用多线程时直接串行

    auto thread = TypeToThreadPool(threadType);
    if (thread) 
        thread->AddQueuedWork(std::make_shared<QueuedWork>(lambda));
}

std::shared_ptr<QueuedThreadPool> EngineThreadPool::TypeToThreadPool(EngineThreadType threadType)
{
    std::shared_ptr<QueuedThreadPool> thread;
    switch (threadType) {
        case ENGINE_THREAD_TYPE_RHI:        thread = rhiThread;     break;
        case ENGINE_THREAD_TYPE_ANY:        thread = anyThread;     break;
        case ENGINE_THREAD_TYPE_MAX_ENUM:                           break;
    }
    return thread;
}