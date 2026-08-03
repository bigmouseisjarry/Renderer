#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <queue>
#include <stack>
#include <string>
#include <vector>

typedef std::chrono::steady_clock::time_point TimePoint;

class TimeScope
{
public:
    TimeScope() { Clear(); }
    ~TimeScope() {}

    std::string name;

    void Clear();
    void Begin();
    void End();
    void EndAfterMilliSeconds(float milliSecondes);
    float GetMicroSeconds();
    float GetMilliSeconds();
    float GetSeconds();

    TimePoint GetBeginTime()    { return begin; }
    TimePoint GetEndTime()      { return end; }
    uint32_t GetDepth()                                     { return depth; }

private:
    TimePoint begin;
    TimePoint end;
    std::chrono::microseconds duration;
    uint32_t depth = 0;
    friend class TimeScopes;
} ;

class TimeScopes
{
public:
    TimeScopes() = default;
    ~TimeScopes() {}

    void PushScope(std::string name);
    void PopScope();
    void Clear();
    bool Valid();
    const std::vector<std::shared_ptr<TimeScope>>& GetScopes();
    const TimePoint& GetBeginTime() { return begin; };
    const TimePoint& GetEndTime() { return end; };
    inline uint32_t Size() const { return scopes.size(); }

private:
    std::vector<std::shared_ptr<TimeScope>> scopes;
    std::stack<std::shared_ptr<TimeScope>> runningScopes;
    uint32_t depth = 0;

    TimePoint begin = std::chrono::steady_clock::now();
    TimePoint end = std::chrono::steady_clock::now();
};

class TimeScopeHelper
{
public:
    TimeScopeHelper(std::string name, TimeScopes* scopes)
    : scopes(scopes)
    {
        scopes->PushScope(name);
    }

    ~TimeScopeHelper()
    {
        scopes->PopScope();
    }

private:
    TimeScopes* scopes;
};