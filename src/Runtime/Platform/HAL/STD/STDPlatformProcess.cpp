#include "STDPlatformProcess.h"
#include "STDRunnableThread.h"
#include "STDSemaphore.h"
#include "STDEvent.h"
#include "STDMutex.h"
#include <thread>
#include <chrono>

RunnableThreadRef STDPlatformProcess::CreateRunnableThread()
{
	return std::make_shared<STDRunnableThread>();
}

void STDPlatformProcess::Sleep(float seconds)
{
	std::this_thread::sleep_for(std::chrono::duration<float>(seconds));
}

SemaphoreRef STDPlatformProcess::CreateSemaphore(uint32_t maxCount)
{
	return std::make_shared<STDSemaphore>(maxCount);
}

SyncEventRef STDPlatformProcess::CreateSyncEvent(bool manualReset)
{
	auto event = std::make_shared<STDEvent>();
	event->Create(manualReset);
	return event;
}

MutexRef STDPlatformProcess::CreateMutex()
{
	return std::make_shared<STDMutex>();
}
