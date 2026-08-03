#include "TimerWidget.h"

#include "Core/Util/TimeScope.h"

#include "imgui_widget_flamegraph.h"
#include <algorithm>
#include <array>
#include <string>

static std::array<TimePoint, 2> period;

static void ProfilerValueGetter(float* startTimestamp, float* endTimestamp, ImU8* level, const char** caption, const void* data, int idx)
{
    auto scopes = (TimeScopes*) data;

    auto& firstScope = scopes->GetScopes()[0];
    auto& scope = scopes->GetScopes()[idx];

    if (startTimestamp)
    {
        auto time = std::chrono::duration_cast<std::chrono::microseconds>(scope->GetBeginTime() - period[0]);
        *startTimestamp = (float)time.count() / 1000;
    }
    if (endTimestamp)
    {
        auto time = std::chrono::duration_cast<std::chrono::microseconds>(scope->GetEndTime() - period[0]);
        *endTimestamp = (float)time.count() / 1000;
    }
    if (level)
    {
        *level = scope->GetDepth();
    }
    if (caption)
    {
        *caption = scope->name.c_str();
    }
}

void TimerWidget::TimeScopeUI(const std::map<uint32_t, std::shared_ptr<TimeScopes>>& timeScopes)
{
    period = {};
    bool init = false;
    for(auto& pair : timeScopes)
    {
        if(pair.second && pair.second->GetScopes().size() > 0)
        {
            auto& begin = pair.second->GetBeginTime();
            auto& end = pair.second->GetEndTime();
            
            period[0] = init ? std::min(begin, period[0]) : begin;
            period[1] = init ? std::max(end, period[1]) : end;
            init = true;
        }
    }
    auto time = std::chrono::duration_cast<std::chrono::microseconds>(period[1] - period[0]);
    float scaleMax = (float)time.count() / 1000;

    for(auto& pair : timeScopes)
    {
        auto scopes = pair.second.get();
        if(scopes)
        {
            std::string name = "[" + std::to_string(pair.first) + "] Thread Time";

            ImGuiWidgetFlameGraph::PlotFlame(
                "", 
                ProfilerValueGetter, 
                scopes, 
                scopes->Size(),
                0,
                name.c_str(), 
                0, 
                scaleMax, 
                ImVec2(0, 0));
        }
    }
}