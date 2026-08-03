#pragma once

#include "Platform/HAL/SyncEvent.h"
#include <mutex>
#include <condition_variable>

class STDEvent : public SyncEvent
{
public:
	STDEvent() : triggered(false), manualReset(false) {}
	virtual ~STDEvent() {}

	virtual bool Create(bool manualReset = false) override
	{
		this->manualReset = manualReset;
		return true;
	}

	virtual bool IsManualReset() override
	{
		return manualReset;
	}

	virtual void Trigger() override
	{
		std::unique_lock<std::mutex> lock(mutex);
		triggered = true;
		condition.notify_all();
	}

	virtual void Reset() override
	{
		std::unique_lock<std::mutex> lock(mutex);
		triggered = false;
	}

	virtual bool Wait(uint32_t waitTime) override
	{
		std::unique_lock<std::mutex> lock(mutex);
		if (waitTime == UINT32_MAX)
		{
			condition.wait(lock, [this] { return triggered; });
		}
		else
		{
			if (!condition.wait_for(lock, std::chrono::milliseconds(waitTime), [this] { return triggered; }))
			{
				return false;
			}
		}

		if (!manualReset)
		{
			triggered = false;
		}

		return true;
	}

private:
	bool triggered;
	bool manualReset;
	std::mutex mutex;
	std::condition_variable condition;
};
