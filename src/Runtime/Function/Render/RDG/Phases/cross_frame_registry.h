#pragma once

#include "Function/Render/RHI/RHICommandList.h"    // RHISemaphoreRef

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

// 跨帧注册表——RDG编译管线唯一的跨帧持久状态（V2方案：rdg-crossframe-registry-v2.md）
//
// 为什么必须是 RenderSystem 拥有、构造注入的共享对象：
// RDGCompiler 是每帧槽一个实例（RenderSystem.h rdgCompilers[FRAMES_IN_FLIGHT]），
// 帧在槽间交替——任何 Phase/编译器成员存跨帧数据，帧N+1（另一槽）读到的都是
// 同槽上一次编译（=帧N-1）的旧数据。文件级 static 语义正确但所有权挂进程、
// 双实例互踩，本类是其正规化替代。
//
// 代际协议：编译期读 current_（上一帧终态）、写 pending_；build_submit_plan 末尾
// commit_frame() 原子换代。主线程逐帧串行访问（编译在主线程，worker只录命令），
// 无需加锁。
//
// 失败语义（定案，勿违反）：编译中途失败=进程fatal，本写入点无需失败回滚——
// 若帧N在"值已写入注册表"后、"vkQueueSubmit2入队"前死掉，该值永不signal，
// 帧N+1的timeline wait将永久挂死（卡死而非崩溃，极难查）。若未来引入可恢复的
// 编译失败路径，必须先把代际提交点挪到SubmitRHI成功入队之后。
//
// FRAMES_IN_FLIGHT=2 前提：资源记录未命中=上帧未触碰⇒安全，依赖"更早帧已被
// 本槽fence等待"的链式归纳。改为3槽须重审此论证。
class RDGCrossFrameRegistry
{
public:
    // 资源在某队列上的末触点（timeline=该队列本帧槽的时间线）
    struct QueuePoint
    {
        RHISemaphoreRef timeline = nullptr;
        uint64_t value = 0;
    };

    // 队列级记录：上一帧各队列的queueDone（粗链/prologue批的数据源）
    struct QueueRecord
    {
        RHISemaphoreRef timeline = nullptr;
        uint64_t doneValue = 0;
    };

    // 资源级记录：imported资源的上帧末触点（细链数据源）。
    // 键=底层RHI对象指针（Build期已挂节点；名字因ping-pong复用不可作键、节点句柄每帧重建不可作键）。
    // per-queue而非单记录：资源上帧在两队列无依赖边并发读时（read-read无边），
    // 本帧首个写者必须等两者——单记录会漏其一（v1方案的理论洞，本版修正）
    struct ResourceRecord
    {
        std::unordered_map<uint32_t, QueuePoint> lastUsePerQueue;
        QueuePoint homingPoint;     // 上帧归巢release的执行点（全局末触批signal；value==0=上帧未归巢）——
                                    // 本帧prologue acquire(F→GFX)的执行序承接（替代整队列queueDone粗wait）
    };

    // 写侧：帧首清pending（current保留——本帧编译期读的是上帧数据）
    void begin_frame(uint32_t queueCount)
    {
        pendingQueues_.assign(queueCount, QueueRecord{});
        pendingResources_.clear();
    }

    // 写侧：build_submit_plan末尾调用，原子换代
    void commit_frame()
    {
        currentQueues_ = std::move(pendingQueues_);
        pendingQueues_.clear();
        currentResources_ = std::move(pendingResources_);
        pendingResources_.clear();
        hasLastFrame_ = true;
    }

    // ---- 队列级 ----
    const QueueRecord* find_queue(uint32_t queue) const
    {
        if (queue >= currentQueues_.size()) return nullptr;
        return &currentQueues_[queue];
    }
    void record_queue(uint32_t queue, QueueRecord&& record)
    {
        if (queue < pendingQueues_.size()) pendingQueues_[queue] = std::move(record);
    }

    // ---- 资源级 ----
    const ResourceRecord* find_resource(const void* key) const
    {
        auto found = currentResources_.find(key);
        return found == currentResources_.end() ? nullptr : &found->second;
    }
    void record_resource_last_use(const void* key, uint32_t queue, QueuePoint point)
    {
        pendingResources_[key].lastUsePerQueue[queue] = std::move(point);
    }
    void record_homing_point(const void* key, QueuePoint point)
    {
        pendingResources_[key].homingPoint = std::move(point);
    }
    // 遍历上帧全部资源记录（prologue承接wait的归巢点扫描用）
    void foreach_resource(const std::function<void(const void*, const ResourceRecord&)>& func) const
    {
        for (const auto& [key, record] : currentResources_) func(key, record);
    }

    // 是否存在上一帧数据（进程首帧为false）
    bool has_last_frame() const { return hasLastFrame_; }

private:
    std::vector<QueueRecord> currentQueues_;                        // 上帧终态（只读）
    std::vector<QueueRecord> pendingQueues_;                        // 本帧写入
    std::unordered_map<const void*, ResourceRecord> currentResources_;
    std::unordered_map<const void*, ResourceRecord> pendingResources_;
    bool hasLastFrame_ = false;
};
