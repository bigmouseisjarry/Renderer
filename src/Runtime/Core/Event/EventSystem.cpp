#include "EventSystem.h"
#include <cstdint>

void EventSystem::Tick()
{
	queue.process();
}

EventHandle* EventSystem::AddListener(const std::function<void(const std::shared_ptr<Event>&)>& callback, EventType eventType, EventDispatchID dispatchID)
{
	EventHandle* eventHandle = new EventHandle;
	eventHandle->type = eventType;
    eventHandle->dispatchID = dispatchID;

	if(dispatchID == UINT64_MAX) eventHandle->handle0 = queue.appendListener(eventHandle->type, callback);
	else 						 eventHandle->handle1 = seperateQueue.appendListener({eventHandle->type, eventHandle->dispatchID}, callback);
	return eventHandle;
}

void EventSystem::RemoveListener(EventHandle*& handle)
{
    if(!handle) return;

	EventHandle* eventHandle = handle;
	if(eventHandle->dispatchID == UINT64_MAX) 	queue.removeListener(eventHandle->type, eventHandle->handle0);
	else						 				seperateQueue.removeListener({eventHandle->type, eventHandle->dispatchID}, eventHandle->handle1);
	delete eventHandle;

    handle = nullptr;
}

void EventSystem::AsyncDispatch(const std::shared_ptr<Event>& event)
{
	queue.enqueue(event);
	if(event->GetDispatchID() != UINT64_MAX) seperateQueue.enqueue(event);
}

void EventSystem::Dispatch(const std::shared_ptr<Event>& event)
{
	queue.dispatch(event);
	if(event->GetDispatchID() != UINT64_MAX) seperateQueue.dispatch(event);
}