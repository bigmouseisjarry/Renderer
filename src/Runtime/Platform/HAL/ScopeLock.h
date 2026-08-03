#pragma once

#include "Platform/HAL/Mutex.h"

#include <cassert>

class ScopeLock
{
public:
	ScopeLock(MutexRef sync)
    : sync(sync)
	{
		assert(sync != nullptr);
		sync->Lock();
	}

	~ScopeLock()
	{
		Unlock();     
	}

	void Unlock()
	{
		if(sync)
		{
			sync->Unlock();
			sync = nullptr;
		}
	}

private:
	ScopeLock();
	ScopeLock(const ScopeLock& scopeLock);
	ScopeLock& operator=( ScopeLock& scopeLock) { return *this; }

private:
	MutexRef sync;
};
