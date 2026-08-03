#pragma once

#include "Core/Util/TimeScope.h"
#include <map>

class TimerWidget
{
public:
    static void TimeScopeUI(const std::map<uint32_t, std::shared_ptr<TimeScopes>>& timeScopes);
};