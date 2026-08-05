#include "EditorSystem.h"
#include "Function/Global/EngineContext.h"
#include "Platform/Input/InputSystem.h"
#include "Widget/HierarchyWidget.h"
#include "Widget/InspectorWidget.h"
#include "Widget/GizmoWidget.h"
#include "Widget/RDGGraphWidget.h"

void EditorSystem::Init()
{

}

void EditorSystem::UI()
{
    if(EngineContext::Input()->IsKeyPressed(SDL_SCANCODE_Q))  show = !show;
    if(show)
    {
        HierarchyWidget::UI();
        InspectorWidget::UI();
        GizmoWidget::UI();
    } 
    // else GizmoWidget::DisableUI();
}