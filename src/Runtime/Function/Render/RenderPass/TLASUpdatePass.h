#pragma once

#include "Function/Render/RHI/RHIStructs.h"
#include "RenderPass.h"

// TLAS更新pass：把每帧的TLAS构建/refit录进帧命令流（原RHI线程上独立提交+等待的UpdateTLAS收编进RDG，
// 每帧一次的流水线停顿消失，TLAS成为图的真实依赖节点——目标B多队列时自动获得跨队列同步点）。
//
// 存储/实例/scratch buffer均为TLAS对象的持久成员，仅存储buffer以imported形式进图：
//   本pass OutputReadWrite（产出边）+ RT pass的Dependency（虚拟读边）→
//   拓扑排序保证本pass先于RT pass，屏障链自动生成为 [after] AS→UAV（等构建写）→ RT pass [before] UAV→SRV
//
// 注意：本pass必须在所有RT pass之前Build（PassType枚举序保证），RT pass经blackboard的
// "TLAS Storage"名字取依赖句柄
class TLASUpdatePass : public RenderPass
{
public:
    TLASUpdatePass() {};
	~TLASUpdatePass() {};

    virtual void Build(RDGBuilder& builder) override final;

    virtual std::string GetName() override final { return "TLAS Update"; }

    virtual PassType GetType() override final { return TLAS_UPDATE_PASS; }
};
