#pragma once

#include <cstdint>
#include <memory>

class SyncEvent	// 事件抽象
{
public:
    SyncEvent() {}
	virtual ~SyncEvent() {}

	virtual bool Create(bool manualReset = false) = 0;
	virtual bool IsManualReset() = 0;
	virtual void Trigger() = 0;
	virtual void Reset() = 0;
	virtual bool Wait(uint32_t WaitTime) = 0;

	bool Wait() { return Wait(UINT32_MAX); }
};

typedef std::shared_ptr<SyncEvent> SyncEventRef;