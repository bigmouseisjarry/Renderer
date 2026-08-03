#pragma once

#include "Platform/Thread/QueuedThreadPool.h"
#include <cstdint>
#include <string>

enum EngineThreadType : uint32_t
{
	ENGINE_THREAD_TYPE_RHI = 0,
	ENGINE_THREAD_TYPE_ANY,

	ENGINE_THREAD_TYPE_MAX_ENUM,	//
};

class EngineThreadPool
{
public:
    void Init();

    void Tick() {};

    uint32_t ThreadFrameIndex();    // 获取线程任务的帧，任务被录制时的帧下标（执行时可能跨帧）
    uint32_t ThreadPreviousFrameIndex();
    uint32_t ThreadTick();    

    void WaitIdle(EngineThreadType threadType = ENGINE_THREAD_TYPE_ANY);
    void WaitAllIdle();
	void Destroy();

	void AddQueuedWork(QueuedWorkFunc func, EngineThreadType threadType = ENGINE_THREAD_TYPE_ANY);

private:
    std::shared_ptr<QueuedThreadPool> TypeToThreadPool(EngineThreadType threadType);

    std::shared_ptr<QueuedThreadPool> rhiThread;
    std::shared_ptr<QueuedThreadPool> anyThread;
    
    static thread_local uint32_t threadFrameIndex; // 
    static thread_local uint32_t threadTick;
    uint32_t mainThreadID = 0;
};

