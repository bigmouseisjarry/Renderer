#pragma once

#include "Platform/HAL/Mutex.h"

#include <windows.h>

class WindowsMutex : public Mutex
{
public:
	WindowsMutex(const WindowsMutex&) = delete;
	WindowsMutex& operator=(const WindowsMutex&) = delete;

	WindowsMutex()
	{
		InitializeCriticalSection(&criticalSection);
		SetCriticalSectionSpinCount(&criticalSection,4000);
	}

	~WindowsMutex()
	{
		DeleteCriticalSection(&criticalSection);
	}

	inline virtual void Lock() override
	{
		EnterCriticalSection(&criticalSection);
	}

	inline virtual bool TryLock() override
	{
		if (TryEnterCriticalSection(&criticalSection))
		{
			return true;
		}
		return false;
	}

	inline virtual void Unlock() override
	{
		LeaveCriticalSection(&criticalSection);
	}

private:
	CRITICAL_SECTION criticalSection;
};