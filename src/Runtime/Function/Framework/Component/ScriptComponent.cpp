#include "ScriptComponent.h"
#include <memory>

CEREAL_REGISTER_TYPE(ScriptComponent)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Component, ScriptComponent)

void ScriptComponent::OnInit()
{
    Component::OnInit();
}

void ScriptComponent::OnUpdate(float deltaTime)
{
    InitComponentIfNeed();

    if(onUpdate)
        onUpdate(GetEntity(), deltaTime);
}
