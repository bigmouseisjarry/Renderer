#pragma once

#include "Function/Render/RHI/RHICommandList.h"    // RHISemaphoreRef

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

// 跨帧注册表——RDG编译管线唯一的跨帧持久状态（时间线双signal模型，定案2026-10-25）
//
// 为什么必须是 RenderSystem 拥有、构造注入的共享对象：
// RDGCompiler 是每帧槽一个实例（RenderSystem.h rdgCompilers[FRAMES_IN_FLIGHT]），
// 帧在槽间交替——任何 Phase/编译器成员存跨帧数据，帧N+1（另一槽）读到的都是
// 同槽上一次编译（=帧N-1）的旧数据。文件级 static 语义正确但所有权挂进程、
// 双实例互踩，本类是其正规化替代。
//
// 两块状态：
//   B（timelineCounters_）——live 非代际：值分配器跨帧延续；主线程逐帧串行访问
//     （编译在主线程，worker只录命令），无需加锁
//   import资源同步记录（current_/pending_）——代际：编译期（Phase 7走查）读current_
//     （上帧终态）、写pending_；build_submit_plan 末尾 commit_frame() 换代（merge语义）
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
    // B的两个职责，拆任一则规则①（同时间线signal按提交序严格递增）被击穿：
    //   跨帧值序——时间线为帧槽私有对象（同槽隔帧复用），帧N与帧N+2提交到同一物理队列，
    //     值必须跨帧延续；
    //   帧内值序——同队列互不依赖的pass（空wait）从依赖链推不出值，靠计数器垫底防撞。
    // 分配公式：signal = max(wait[本时间线], B[本时间线]) + 1。
    // 返回的值可能最终未被任何wait引用（分配后节点被合并等）——空洞无害，计数器照常推进
    uint64_t alloc_timeline_signal(const void* timeline, uint64_t lowerBound)
    {
        uint64_t& counter = timelineCounters_[timeline];
        counter = std::max(counter, lowerBound);
        return ++counter;
    }
    // B读取（不分配）：池化种子快照用——本槽各时间线的上代末值（槽fence已保证其完成，waits瞬过）
    uint64_t peek_timeline_counter(const void* timeline) const
    {
        auto found = timelineCounters_.find(timeline);
        return found == timelineCounters_.end() ? 0 : found->second;
    }
    // 计数器seed（max更新）：B于进程首见每条时间线时从0自起，此接口留作迁移/诊断用
    void seed_timeline_counter(const void* timeline, uint64_t value)
    {
        uint64_t& counter = timelineCounters_[timeline];
        counter = std::max(counter, value);
    }

    // import资源同步记录：上帧末的同步状态
    struct ResourceSyncRecord
    {
        uint32_t owner_queue = 0;   // 末触队列（族:队列=1:1，跨族判定即跨队列判定）
        // 时间线对象 → {同族signal, 异族signal}：
        //   同族值 = 该线最后一次write的signal（同队列冲突由Phase 7状态屏障覆盖，信号量不管）
        //   异族值 = 该线最后一次任意访问的signal（跨队列RAW/WAR/WAW全覆盖）
        // 键持RHISemaphoreRef而非裸指针：记录里含上帧（另一槽）的时间线对象，须持引用
        // 跨帧保活至被wait完
        std::unordered_map<RHISemaphoreRef, std::pair<uint64_t, uint64_t>> signals;
        // 上帧末真实布局（逐子资源，mip主序展开=mip*arrayLayers+layer，与Phase 7 tracker同构）。
        // 布局动态记载制（2026-10-26）：import的布局种子权威——调用点Import声明退役为首见兜底。
        // 与signals同代际同语义：他槽在飞帧的记录，其值在下帧wait该帧signals后即为真实
        std::vector<RHIResourceState> final_states;
    };
    const ResourceSyncRecord* find_resource_sync(const void* key) const
    {
        auto found = currentSyncRecords_.find(key);
        return found == currentSyncRecords_.end() ? nullptr : &found->second;
    }
    void record_resource_sync(const void* key, ResourceSyncRecord&& record)
    {
        pendingSyncRecords_[key] = std::move(record);
    }

    // build_submit_plan末尾调用，原子换代
    void commit_frame()
    {
        // merge换代而非整表替换：上帧未触碰的import资源（本帧未写pending键），
        // 其所有权仍停在上上次触碰的族——槽fence保证那些使用已完成，但族信息依然有效且
        // 下帧悬空起点判定必需；整表替换会丢族→悬空漏发→跨族无release（validation必抓）。
        // 旧signal值已signal，下帧wait瞬间通过，无害
        for (auto& [key, record] : pendingSyncRecords_) currentSyncRecords_[key] = std::move(record);
        pendingSyncRecords_.clear();
    }

private:
    std::unordered_map<const void*, uint64_t> timelineCounters_;             // B（live：跨帧延续，非代际）
    std::unordered_map<const void*, ResourceSyncRecord> currentSyncRecords_; // 上帧终态（只读）
    std::unordered_map<const void*, ResourceSyncRecord> pendingSyncRecords_; // 本帧写入
};
