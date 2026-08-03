#include "Clipmap.h"
#include "Core/Math/Math.h"
#include <cfloat>
#include <cmath>
#include <cstdlib>

// Vec3 Clipmap::FetchClipmapUVW(Vec3 position, float extent)
// {
//     Vec3 temp = position / extent;
//     return temp - Vec3(floor(temp.x()), floor(temp.y()), floor(temp.z()));
// }

// Vec3 Clipmap::FetchClipmapUVW(Vec3 position, Vec3 extent)
// {
//     Vec3 temp = position.array() / extent.array();
//     return temp - Vec3(floor(temp.x()), floor(temp.y()), floor(temp.z()));
// }

// float Clipmap::FetchClipmapHalfExtent(const ClipmapLevel& level)
// {
//     float halfExtent = level.voxelSize * (float(level.extent) / 2); 
//     return halfExtent;
// }

// IVec3 Clipmap::FetchClipmapSampleCoord(const ClipmapLevel& level, Vec3 worldPos, int layer)
// {
//     Vec3 halfExtent = Vec3::Ones() * FetchClipmapHalfExtent(level); 

// 	worldPos = worldPos.cwiseMax(level.center - halfExtent)
//                        .cwiseMin(level.center + halfExtent);
	
// 	Vec3 clipCoords = FetchClipmapUVW(worldPos, halfExtent * 2);
//     clipCoords *= level.extent;

// 	IVec3 imageCoords = IVec3(int(floor(clipCoords.x())), int(floor(clipCoords.y())), int(floor(clipCoords.z())) );
//     for(int i = 0; i < 3; i++)  imageCoords[i] = imageCoords[i] % (level.extent);
    
// 	imageCoords.y() += level.extent * level.level; 
//     imageCoords.z() += level.extent * layer;
	
// 	return imageCoords;
// }

// Vec3 Clipmap::FetchClipmapPos(const ClipmapLevel& level, IVec3 clipmapCoord)
// {
// 	float voxelSize = level.voxelSize;
//     Vec3 halfExtent = Vec3::Ones() * FetchClipmapHalfExtent(level);  

//     clipmapCoord = clipmapCoord.cwiseMax(IVec3(0)) 
//                                .cwiseMin(IVec3(level.extent - 1)); 

//     Vec3 worldPos = level.center - halfExtent + (Vec3(clipmapCoord.x(), clipmapCoord.y(), clipmapCoord.z()) + Vec3(0.5, 0.5, 0.5)) * voxelSize;
//     return worldPos;
// }

Clipmap::Clipmap(int mipLevel, int extent, float minVoxelSize)
: extent(extent)
, minVoxelSize(minVoxelSize)
{
    info.totalMipLevel = mipLevel;
    for(int i = 0; i < LevelCount(); i++)
    {
        auto& level = info.levels[i];
        level.center = Vec3(0, 0, 0); 
        level.level = i;
        level.extent = extent;
        level.voxelSize = std::pow(2, i) * minVoxelSize;
    }
}

std::vector<ClipmapRegion> Clipmap::Update(Vec3 newCenter)
{
    std::vector<ClipmapRegion> updateRegions;
    for(int i = 0; i < LevelCount(); i++)
    {
        auto& level = info.levels[i];

        float minUpdateSize = level.voxelSize * 2;  // 以两格为基本单位进行更新

        Vec3 deltaCenter = newCenter - level.center;
        IVec3 delta2Voxel = IVec3(floor(deltaCenter.x() / minUpdateSize),
                                 floor(deltaCenter.y() / minUpdateSize),
                                 floor(deltaCenter.z() / minUpdateSize));
        IVec3 absDeltaVoxel = IVec3(abs(delta2Voxel.x()),
                                    abs(delta2Voxel.y()),
                                    abs(delta2Voxel.z())) * 2;        
                                                                                                          
        level.center += Vec3(delta2Voxel.x(), delta2Voxel.y(), delta2Voxel.z()) * minUpdateSize;

        if( !init ||
            absDeltaVoxel.x() >= level.extent || 
            absDeltaVoxel.y() >= level.extent || 
            absDeltaVoxel.z() >= level.extent)      // 全量更新
        {
            updateRegions.push_back(ClipmapRegion(IVec3(0, 0, 0), IVec3(extent, extent, extent), i));
            continue;
        }

        IVec3 min = IVec3(0, 0, 0);             // 增量更新，有一些重复部分
        IVec3 max = IVec3(extent, extent, extent);
        if(absDeltaVoxel.x() >= 2)  // X方向更新
        {
            IVec3 regionExtent = IVec3(absDeltaVoxel.x(), level.extent, level.extent);

            if(delta2Voxel.x() > 0) updateRegions.push_back(ClipmapRegion(max - regionExtent, regionExtent, i));
            else                    updateRegions.push_back(ClipmapRegion(min, regionExtent, i));
        }
        if(absDeltaVoxel.y() >= 2)  // Y方向更新
        {
            IVec3 regionExtent = IVec3(level.extent, absDeltaVoxel.y(), level.extent);

            if(delta2Voxel.y() > 0) updateRegions.push_back(ClipmapRegion(max - regionExtent, regionExtent, i));
            else                    updateRegions.push_back(ClipmapRegion(min, regionExtent, i));
        }
        if(absDeltaVoxel.z() >= 2)  // Z方向更新
        {
            IVec3 regionExtent = IVec3(level.extent, level.extent, absDeltaVoxel.z());

            if(delta2Voxel.z() > 0) updateRegions.push_back(ClipmapRegion(max - regionExtent, regionExtent, i));
            else                    updateRegions.push_back(ClipmapRegion(min, regionExtent, i));
        }
    }

    init = true;
    return updateRegions;
}
