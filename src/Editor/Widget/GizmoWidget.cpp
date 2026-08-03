#include "GizmoWidget.h"
#include "Core/Math/Math.h"
#include "Function/Framework/Component/TransformComponent.h"
#include "Function/Global/EngineContext.h"

#include <imgui.h>
#include "Function/Render/RenderPass/RenderPass.h"
#include "ImGuizmo.h"

void GizmoWidget::UI()
{
    static bool enable = true;
    static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);
	static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::WORLD);

    EngineContext::Render()->GetPasses()[GIZMO_PASS]->SetEnable(enable);    // 在这里控制Gizmo pass的启用

    ImGui::Begin("Gizmo Window", NULL, ImGuiWindowFlags_NoTitleBar);

    ImGui::Checkbox("Gizmo", &enable);
    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

	if (ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE))
		mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
	ImGui::SameLine();
	if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE))
		mCurrentGizmoOperation = ImGuizmo::ROTATE;
	ImGui::SameLine();
	if (ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE))
		mCurrentGizmoOperation = ImGuizmo::SCALE;

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    if (ImGui::RadioButton("Local", mCurrentGizmoMode == ImGuizmo::LOCAL))
        mCurrentGizmoMode = ImGuizmo::LOCAL;
    ImGui::SameLine();
    if (ImGui::RadioButton("World", mCurrentGizmoMode == ImGuizmo::WORLD))
        mCurrentGizmoMode = ImGuizmo::WORLD;

    ImGui::End();


	
    auto entity = EngineContext::Editor()->GetSelectedEntity();
    auto camera = EngineContext::World()->GetActiveScene()->GetActiveCamera();
    if(enable && entity && entity->TryGetComponent<TransformComponent>() && camera)
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

        auto transform = entity->TryGetComponent<TransformComponent>();

        // Vec3 position = transform->GetPosition();
        // Vec3 rotation = transform->GetEulerAngle();
        // Vec3 scale = transform->GetScale();

        // ImGuizmo::DecomposeMatrixToComponents((float*)&model, &position(0, 0), &rotation(0, 0), &scale(0, 0));
        // ImGuizmo::RecomposeMatrixFromComponents(&position(0, 0), &rotation(0, 0), &scale(0, 0), (float*)&model);
        // transform->SetPosition(position);
        // transform->SetRotation(rotation);
        // transform->SetScale(scale);

        Mat4 view = camera->GetViewMatrix();
        Mat4 proj = camera->GetProjectionMatrix();
        proj(1, 1) *= -1;	//这个库的手性和opengl是一样的

        // 用这个库本身提供的RecomposeMatrixFromComponents，由于旋转顺序不同会有死锁问题，因此直接做矩阵计算了
        Mat4 model = transform->GetModelMat();
        Mat4 delta;
        ImGuizmo::Manipulate((float*)&view, (float*)&proj, mCurrentGizmoOperation, mCurrentGizmoMode, (float*)&model, (float*)&delta, NULL);
        if(delta != Mat4::Identity()) transform->SetTransform(model);  // 做个判断，不然参数输入和这里会冲突
    }	
}

void GizmoWidget::DisableUI()
{
    EngineContext::Render()->GetPasses()[GIZMO_PASS]->SetEnable(false);    // 在这里控制Gizmo pass的启用
}