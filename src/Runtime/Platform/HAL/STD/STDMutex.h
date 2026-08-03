#pragma once

#include "Platform/HAL/Mutex.h"
#include <mutex>

class STDMutex : public Mutex
{
public:
    STDMutex() {};
	virtual ~STDMutex() {};

	virtual void Lock() override
	{
		mutex.lock();
	}
	virtual bool TryLock() override
	{
		return mutex.try_lock();
	}
	virtual void Unlock() override
	{
		mutex.unlock();
	}
private:
	std::mutex mutex;
};
