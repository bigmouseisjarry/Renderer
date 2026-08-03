#pragma once

#include "Event.h"
#include "Function/Render/RenderResource/Material.h"

#include <string>

#define SimpleEvent(eventClass, eventType)                  	\
class eventClass : public Event                             	\
{                                                           	\
public:                                                     	\
	eventClass(EventDispatchID dispatchID = UINT64_MAX)     	\
    : Event(dispatchID) {}                       				\
																\
	virtual EventType GetType() override { return eventType; }	\
};                                                          

SimpleEvent(BeforeUpdateEvent, EVENT_BEFORE_UPDATE)
SimpleEvent(OnUpdateEvent, EVENT_ON_UPDATE)
SimpleEvent(AfterUpdateEvent, EVENT_AFTER_UPDATE)

class MaterialUpdateEvent : public Event
{
public:
	MaterialUpdateEvent(Material* material, EventDispatchID dispatchID = UINT64_MAX) 
    : Event(dispatchID), material(material) {}

	virtual EventType GetType() override { return EVENT_MATERIAL_UPDATE; }

	Material* material;
};

class MessageEvent : public Event
{
public:
	MessageEvent(std::string str, EventDispatchID dispatchID = UINT64_MAX) 
    : Event(dispatchID), message(str) {}

	virtual EventType GetType() override { return EVENT_MESSAGE; }

	std::string message;
};


