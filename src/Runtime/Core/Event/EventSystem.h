#pragma once

#include "Types.h"
#include "eventpp/eventqueue.h"
#include "Event.h"
#include <cstdint>
#include <utility>

struct SepearateEventPolicy
{
	static std::pair<uint64_t, uint64_t> getEvent(const std::shared_ptr<Event>& event)	//用于指明如何获取事件类型
	{
		return {event->GetType(), event->GetDispatchID()};
	}
};
struct EventPolicy
{
	static uint64_t getEvent(const std::shared_ptr<Event>& event)
	{
		return event->GetType();
	}
};
using SepearateEventQueue = eventpp::EventQueue<std::pair<uint64_t, uint64_t>, void(const std::shared_ptr<Event>&), SepearateEventPolicy>;
using EventQueue = eventpp::EventQueue<uint64_t, void(const std::shared_ptr<Event>&), EventPolicy>;
using EventDispatchID = uint64_t;

struct EventHandle
{
	EventType type;
    EventDispatchID dispatchID;
	EventQueue::Handle handle0;
	SepearateEventQueue::Handle handle1;
};

class EventSystem
{
public:
	void Init() {}
	void Tick();
	void Destroy() {}

	EventHandle* AddListener(const std::function<void(const std::shared_ptr<Event>&)>& callback, EventType eventType, EventDispatchID dispatchID = UINT64_MAX);
	void RemoveListener(EventHandle*& handle);
	void AsyncDispatch(const std::shared_ptr<Event>& event);
	void Dispatch(const std::shared_ptr<Event>& event);

private:
	EventQueue queue;
	SepearateEventQueue seperateQueue;
};