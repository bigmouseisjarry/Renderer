#pragma once

#include "Core/Math/Math.h"
#include <cmath>
#include <vector>

// clipmap每一级mip的信息
typedef struct ClipmapLevel
{
    Vec3 center;
    float _padding1;

    int level;
    int extent;
    float voxelSize;
    float _padding2;

} ClipmapLevel;

// 提供给GPU端的clipmap信息
typedef struct ClipmapInfo
{
    int totalMipLevel = 0;
    int _padding[3];

    ClipmapLevel levels[32];    // 32级再怎么也应该够了
} ClipmapInfo;

// 描述clipmap内某一部分区域的信息，用于更新等
typedef struct ClipmapRegion
{
    IVec3 min;
    int _padding1;
    IVec3 extent;
    int _padding2;

    int mipLevel;

    ClipmapRegion() {}
    ClipmapRegion(IVec3 min, IVec3 extent, int mipLevel)
    : min(min)
    , extent(extent)
    , mipLevel(mipLevel)
    {}

} ClipmapRegion;

// clipmap的每级具有不同的中心，可以分级进行更新
// 目前设为每级对应2个体素边长的位移将导致中心的更新，并需要更新边界处的体素信息
// 所有更新的区域范围将对齐2体素
// 这也要求更高级的mip需要存储1体素前一级mip的边缘体素信息，以保证不会在各mip边界处产生空隙
class Clipmap
{
public:
    // static Vec3 FetchClipmapUVW(Vec3 position, float extent);
    // static Vec3 FetchClipmapUVW(Vec3 position, Vec3 extent);
    // static float FetchClipmapHalfExtent(const ClipmapLevel& level);
    // static IVec3 FetchClipmapSampleCoord(const ClipmapLevel& level, Vec3 worldPos, int layer);
    // static Vec3 FetchClipmapPos(const ClipmapLevel& level, IVec3 clipmapCoord);

public:
    Clipmap(int mipLevel, int extent, float minVoxelSize);

    std::vector<ClipmapRegion> Update(Vec3 newCenter);

    inline const ClipmapRegion GetRegion(int mipLevel)  { return ClipmapRegion(IVec3(0, 0, 0), IVec3(extent, extent, extent), mipLevel); }
    inline const ClipmapLevel& GetLevel(int mipLevel)   { return info.levels[mipLevel]; }
    inline const ClipmapInfo& GetInfo()                 { return info; }

    inline int LevelCount()                             { return info.totalMipLevel; }
    inline int Extent()                                 { return extent; }
    inline int VoxelSize(int mipLevel = 0)              { return std::pow(2, mipLevel) * minVoxelSize; }

private:
    ClipmapInfo info;

    int extent = 0;
    float minVoxelSize = 0;
    bool init = false;
};