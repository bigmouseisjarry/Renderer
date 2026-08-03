#pragma once

#include "Component.h"
#include "Core/Serialize/Serializable.h"

#include <cstdint>
#include <functional>

typedef std::function<void(std::shared_ptr<Entity>, float)> ScriptFunction;

class ScriptComponent : public Component    // 临时的脚本组件
{
public:
	ScriptComponent() {};
	~ScriptComponent() {};

	virtual void OnInit() override;
	virtual void OnUpdate(float deltaTime) override;

    virtual std::string GetTypeName() override		{ return "Script Component"; }
	virtual ComponentType GetType() override	    { return SCRIPT_COMPONENT; }

    void ScriptOnUpdate(ScriptFunction func)        { onUpdate = func; }

private:
    ScriptFunction onUpdate;

private:
    BeginSerailize()
    SerailizeBaseClass(Component)
    EndSerailize

    EnableComponentEditourUI()
};