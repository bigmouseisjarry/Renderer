#pragma once

#include <cstdint>

enum EventType : uint64_t
{
	EVENT_DEFAULT = 0,
	
    EVENT_BEFORE_UPDATE,
    EVENT_ON_UPDATE,
    EVENT_AFTER_UPDATE,

    EVENT_MATERIAL_UPDATE,
    EVENT_MESSAGE,
};

using EventDispatchID = uint64_t;

class Event
{
public:
	Event(EventDispatchID dispatchID = UINT64_MAX) 
    : dispatchID(dispatchID) 
    {}

    EventDispatchID GetDispatchID() { return dispatchID; }
    virtual EventType GetType() = 0;

private:
    EventDispatchID dispatchID;
};


