#pragma once

#include "Platform/HAL/RunnableThread.h"
#include <thread>
#include <chrono>

class STDRunnableThread : public RunnableThread
{
public:
	STDRunnableThread() {}
	virtual ~STDRunnableThread()
	{
		if (thread.joinable())
		{
			thread.join();
		}
	}

	virtual void SetThreadPriority(ThreadPriorityType priority) override
	{
		// std::thread does not support setting priority
	}

	virtual bool SetThreadAffinity(uint64_t affinity) override
	{
		// std::thread does not support setting affinity
		return false;
	}

	virtual void Suspend(bool pause = true) override
	{
		// std::thread does not support suspend/resume
	}

	virtual bool Kill(bool wait = true) override
	{
		// std::thread does not support killing
		return false;
	}

	virtual void WaitForCompletion() override
	{
		if (thread.joinable())
		{
			thread.join();
		}
	}

protected:
	virtual bool CreateInternal(
		RunnableRef runnable,
		uint32_t stackSize = 0,
		ThreadPriorityType priority = PRIORITY_TYPE_NORMAL,
		uint64_t affinityMask = 0,
		const std::string& name = "") override
	{
		this->runnable = runnable;
		this->name = name;
		this->priority = priority;
		this->affinityMask = affinityMask;

		thread = std::thread([this]() {
			this->runnable->Run();
			});

		this->id = std::hash<std::thread::id>{}(thread.get_id());
		return true;
	}

private:
	std::thread thread;
};
