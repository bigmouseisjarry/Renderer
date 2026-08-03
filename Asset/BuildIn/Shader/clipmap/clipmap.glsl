#ifndef CLIPMAP_GLSL
#define CLIPMAP_GLSL

struct ClipmapLevel
{
    vec3 center;
    float _padding1;

    int level;
    int extent;
    float voxelSize;
    float _padding2;
};

struct ClipmapInfo
{
    int totalMipLevel;
    int _padding[3];

    ClipmapLevel levels[32];    // 32级再怎么也应该够了
};

struct ClipmapRegion
{
    ivec3 min;
    int _padding1;
    ivec3 extent;
    int _padding2;

    int mipLevel;
};

// Y轴 延伸mipLevel倍
// Z轴 延伸6倍（6个表面, 按下标顺序为前，后，上，下，右，左）（+X, -X, +Y, -Y, +Z, -Z）

// 根据trace方向选择对应的三个layer (与主方向相反的面)
ivec3 FetchClipmapLayers(vec3 traceDir)
{
    return ivec3((traceDir.x > 0) ? 1 : 0,
                 (traceDir.y > 0) ? 3 : 2,
                 (traceDir.z > 0) ? 5 : 4);

    // vec3 absDir = abs(traceDir);
    // if(absDir.x >= absDir.y && absDir.x >= absDir.z)        return (traceDir.x > 0) ? 1 : 0;   
    // else if(absDir.y >= absDir.x && absDir.y >= absDir.z)   return (traceDir.y > 0) ? 3 : 2;   
    // else                                                    return (traceDir.z > 0) ? 5 : 4;    
}

// 在clipmap中的UV坐标，[0, 1] (repeat, toroidal addressing)
vec3 FetchClipmapUVW(vec3 position, float extent)
{
	return fract(position / extent);
}

vec3 FetchClipmapUVW(vec3 position, vec3 extent)
{
	return fract(position / extent);
}

// 从clipmap层级到包围盒尺寸
float FetchClipmapHalfExtent(in ClipmapLevel level)
{
    float halfExtent = level.voxelSize * (level.extent / 2); 
    return halfExtent;
}

// 从世界坐标到clipmap的mip层级
int FetchClipmapMipLevel(in ClipmapInfo clipmap, vec3 worldPos)
{
    vec3 halfExtent = vec3(clipmap.levels[0].voxelSize * (clipmap.levels[0].extent / 2)); 
    ivec3 mip = ivec3(ceil(log2(abs(worldPos - clipmap.levels[0].center) / halfExtent)));

    return clamp(max(mip.x, max(mip.y, mip.z)), 0, clipmap.totalMipLevel - 1);
}

// 从世界坐标到clipmap物理索引(与level.center解耦，是循环寻址的)
ivec3 FetchClipmapSampleCoord(in ClipmapLevel level, vec3 worldPos, int layer)
{
    vec3 halfExtent = vec3(FetchClipmapHalfExtent(level)); 

	worldPos = clamp(worldPos, level.center - halfExtent, level.center + halfExtent);
	
	vec3 clipCoords = FetchClipmapUVW(worldPos, halfExtent * 2);

	ivec3 imageCoords = ivec3(clipCoords * level.extent) % (level.extent);
    
	imageCoords.y += level.extent * level.level; 
    imageCoords.z += level.extent * layer;
	
	return imageCoords;
}

// 从clipmap虚拟索引到体素中心的世界坐标(这里的索引以体素边界为0~extent，与物理索引并不相同)
vec3 FetchClipmapPos(in ClipmapLevel level, ivec3 clipmapCoord)
{
	float voxelSize = level.voxelSize;
    vec3 halfExtent = vec3(FetchClipmapHalfExtent(level));  

    clipmapCoord = clamp(clipmapCoord, ivec3(0), ivec3(level.extent - 1));

    vec3 worldPos = level.center - halfExtent + (clipmapCoord + vec3(0.5)) * voxelSize;
    return worldPos;
}

#endif