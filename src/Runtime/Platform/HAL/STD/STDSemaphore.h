#pragma once

#include "Platform/HAL/Semaphore.h"
#include <mutex>
#include <condition_variable>

class STDSemaphore : public Semaphore
{
public:
	STDSemaphore(uint32_t maxCount)
	: Semaphore(maxCount)
	, count(maxCount) 
	{}

	virtual void Lock() override
	{
		std::unique_lock<std::mutex> lock(mutex);
		condition.wait(lock, [this]() { return count > 0; });
		--count;
	}

	virtual bool TryLock(uint64_t milliseconds) override
	{
		std::unique_lock<std::mutex> lock(mutex);
		if (!condition.wait_for(lock, std::chrono::milliseconds(milliseconds), [this]() { return count > 0; }))
		{
			return false;
		}
		--count;
		return true;
	}

	virtual void Unlock() override
	{
		std::unique_lock<std::mutex> lock(mutex);
		++count;
		condition.notify_one();
	}

private:
	int count;
	std::mutex mutex;
	std::condition_variable condition;
};
