#include "PlatformProcess.h"
#include "Windows/WindowsPlatformProcess.h" // TODO

RunnableThreadRef PlatformProcess::CreateRunnableThread() { return WindowsPlatformProcess::CreateRunnableThread(); }

void PlatformProcess::Sleep(float seconds) { WindowsPlatformProcess::Sleep(seconds); }

uint32_t PlatformProcess::GetThreadID() { return WindowsPlatformProcess::GetThreadID(); }

SemaphoreRef PlatformProcess::CreateSemaphore(uint32_t maxCount) {return WindowsPlatformProcess::CreateSemaphore(maxCount); }

SyncEventRef PlatformProcess::CreateSyncEvent(bool manualReset) { return WindowsPlatformProcess::CreateSyncEvent(manualReset); }	

MutexRef PlatformProcess::CreateMutex() { return WindowsPlatformProcess::CreateMutex(); }

