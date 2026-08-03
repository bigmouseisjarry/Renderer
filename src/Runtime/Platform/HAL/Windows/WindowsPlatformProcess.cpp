#include "WindowsPlatformProcess.h"
#include "WindowsRunnableThread.h"
#include "WindowsSemaphore.h"
#include "WindowsEvent.h"
#include "WindowsMutex.h"

#include <memory>
#include <minwindef.h>
#include <processthreadsapi.h>

#undef CreateSemaphore
#undef CreateMutex

//线程 ////////////////////////////////////////////////////////////////////////////////////////////////////////
RunnableThreadRef WindowsPlatformProcess::CreateRunnableThread() 
{ 
    return std::make_shared<WindowsRunnableThread>(); 
}

void WindowsPlatformProcess::Sleep(float seconds)
{
	uint32_t milliseconds = (uint32_t)(seconds * 1000.0);
	if (milliseconds == 0)	::SwitchToThread();
	else					::Sleep(milliseconds);
}

uint32_t WindowsPlatformProcess::GetThreadID()
{
	DWORD threadID = GetCurrentThreadId();
	return threadID;
}

//信号量 ////////////////////////////////////////////////////////////////////////////////////////////////////////
SemaphoreRef WindowsPlatformProcess::CreateSemaphore(uint32_t maxCount) 
{
    return std::make_shared<WindowsSemaphore>(maxCount); 
}

//事件 ////////////////////////////////////////////////////////////////////////////////////////////////////////
SyncEventRef WindowsPlatformProcess::CreateSyncEvent(bool manualReset)
{
	SyncEventRef event = std::make_shared<WindowsEvent>();	
	if (!event->Create(manualReset)) event = NULL;

	return event;
}

//临界区 ////////////////////////////////////////////////////////////////////////////////////////////////////////
MutexRef WindowsPlatformProcess::CreateMutex()
{
	return std::make_shared<WindowsMutex>();
}


