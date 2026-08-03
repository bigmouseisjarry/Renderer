#pragma once

#include <memory>

class Mutex	// 临界区抽象
{
public:
    Mutex() {};
	Mutex(const Mutex&) = delete;
	Mutex& operator=(const Mutex&) = delete;

	inline virtual void Lock() = 0;
	inline virtual bool TryLock() = 0;
	inline virtual void Unlock() = 0;
};
typedef std::shared_ptr<Mutex> MutexRef;