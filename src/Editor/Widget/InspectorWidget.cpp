#include "InspectorWidget.h"
#include "ComponentWidget.h"
#include "Core/Util/TimeScope.h"
#include "Function/Global/EngineContext.h"
#include "PassWidget.h"
#include "SurfaceCacheWidget.h"
#include "TimerWidget.h"
#include "RDGGraphWidget.h"

#include <cstdint>
#include <imgui.h>
#include <memory>
#include <queue>
#include <unordered_map>

void InspectorWidget::UI()
{
	ImGui::Begin("window##1");

	if (ImGui::BeginTabBar("tab_bar##1"))
	{
		if (ImGui::BeginTabItem("Inspector"))
		{
            InspectorUI();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Pass"))
		{
			PassUI();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Info"))
		{
			InfoUI();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Style"))
		{
			ImGui::ShowStyleEditor();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
	ImGui::Separator();

	ImGui::End();
}

void InspectorWidget::InspectorUI()
{
	auto entity = EngineContext::Editor()->GetSelectedEntity();
	if(entity)	
	{
		for(auto& component : entity->GetComponents())
		{
			ComponentWidget::UI(component);
		}
	}
}

void InspectorWidget::PassUI()
{	
	SurfaceCacheWidget::UI();
	ImGui::SameLine();
	RDGGraphWidget::UI();
	for(auto& pass : EngineContext::Render()->GetPasses())
	{
		if(pass) PassWidget::UI(pass);
	}
}

void InspectorWidget::InfoUI()
{
	// 全局参数设置
	{
		ImGui::SeparatorText("Global Setting");
		auto globalSetting = EngineContext::Render()->GetGlobalSetting();

		ImGui::Text("Total running time: %.1f ms", globalSetting->totalTickTime);
		ImGui::Text("Total running ticks: %d tk", globalSetting->totalTicks);

		ImGui::DragFloat("Min frame time (ms)", &globalSetting->minFrameTime, 0.1f, 0.0f, 100.0f);

		ImGui::Text("Cluster inspect mode");
		ImGui::SameLine();
		if (ImGui::RadioButton("Shaded", globalSetting->clusterInspectMode == 0))
			globalSetting->clusterInspectMode = 0;
		ImGui::SameLine();
		if (ImGui::RadioButton("Cluster", globalSetting->clusterInspectMode == 1))
			globalSetting->clusterInspectMode = 1;
		ImGui::SameLine();
		if (ImGui::RadioButton("Triangle", globalSetting->clusterInspectMode == 2))
			globalSetting->clusterInspectMode = 2;
		ImGui::SameLine();
		if (ImGui::RadioButton("Hybrid", globalSetting->clusterInspectMode == 3))
			globalSetting->clusterInspectMode = 3;
		ImGui::Separator();

		ImGui::DragFloat("Ray tracing offset", &globalSetting->rayTracingOffset, 0.01f);

		ImGui::Separator();
		static bool surfaceCacheUpdate = true;
		static bool surfaceCacheFixScale = false;
		ImGui::Checkbox("Update surface cache", &surfaceCacheUpdate);
		ImGui::SameLine();
		ImGui::Checkbox("Surface cache fix scale", &surfaceCacheFixScale);

		EngineContext::Render()->SetSurfaceCacheUpdate(surfaceCacheUpdate);
		EngineContext::Render()->SetSurfaceCacheFixScale(surfaceCacheFixScale);

	}

	// 帧数统计
	{
		static std::queue<float> frameTimes;
		static float totalFrameTime = 0.0f;

		if(frameTimes.size() >= 100)
		{
			totalFrameTime -= frameTimes.front();
			frameTimes.pop();
		}	
		float newFrameTime = EngineContext::GetDeltaTime();
		totalFrameTime += newFrameTime;
		frameTimes.push(newFrameTime);

		ImGui::Text("Average frame time : %f ms", totalFrameTime / frameTimes.size());
		ImGui::Text("Average frame fps : %f ", 1000.0f / (totalFrameTime / frameTimes.size()));

		// ImGui::Separator();
	}

	// 火焰图
	{
		ImGui::SeparatorText("Flame Graph");

		static bool update = false;
		static std::map<uint32_t, std::shared_ptr<TimeScopes>> previousTimers;

		ImGui::Checkbox("Update", &update);
		if(update)
		{
			previousTimers = EngineContext::GetHistoryTimeScopes();
		}
		ImGui::SameLine();
		if(ImGui::Button("Update once"))
		{
			previousTimers = EngineContext::GetHistoryTimeScopes();
		}

		if(previousTimers.empty()) previousTimers = EngineContext::GetHistoryTimeScopes();
		TimerWidget::TimeScopeUI(previousTimers);
	}
	
}