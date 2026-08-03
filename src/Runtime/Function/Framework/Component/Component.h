#pragma once

#include "Core/Serialize/Serializable.h"
#include "Function/Render/RenderResource/RenderStructs.h"

#include <memory>
#include <string>

class Entity;

enum ComponentType			
{
	UNDEFINED_COMPONENT = 0,

	TRANSFORM_COMPONENT,
	CAMERA_COMPONENT,
	POINT_LIGHT_COMPONENT,
	DIRECTIONAL_LIGHT_COMPONENT,
	VOLUME_LIGHT_COMPONENT,
	MESH_RENDERER_COMPONENT,
	SKYBOX_COMPONENT,
	SCRIPT_COMPONENT,

	COMPONENT_TYPE_MAX_ENUM, //
};

class Component
{
public:
	Component() = default;
	virtual ~Component() {};

	virtual void OnLoad() {};						// 文件操作接口，负责序列化时加载和存储
	virtual void OnSave() {};
	virtual void OnInit() { init = true; }			// 初始化接口
	virtual void OnUpdate(float deltaTime) = 0;		// 循环接口，执行功能逻辑

	inline bool Inited() 						{ return init; }

	virtual std::string GetTypeName()			{ return "Undefined"; }
	virtual ComponentType GetType()				{ return UNDEFINED_COMPONENT; }
	inline std::shared_ptr<Entity> GetEntity()	{ return entity.lock(); }

	template<typename TComponent>
    std::shared_ptr<TComponent> TryGetComponent();

	template<typename TComponent>
    std::shared_ptr<TComponent> TryGetComponentInParent(bool self = false);

private:
	bool init = false;
	
	std::weak_ptr<Entity> entity;	 
	friend class Entity;

private:
    BeginSerailize()
    EndSerailize
};

#define EnableComponentEditourUI() \
friend class ComponentWidget;

#define InitComponentIfNeed() \
if (!Inited()) OnInit();
