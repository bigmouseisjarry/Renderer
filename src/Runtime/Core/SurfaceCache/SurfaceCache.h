#pragma once

#include "Core/Math/Math.h"
#include "Function/Global/Definations.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <sys/stat.h>
#include <vector>

// 以连续的水平块为单位进行分配和释放，管理简单
// 块边长	每行块数	 行数	  总块数	  竖直像素偏移	
// 128		32			16	  	512			0
// 64 		64			16		1024		2048
// 32 		128			16		2048		3072
// 16 		256			16		4096		3584
// 8		512			32		16384		3840
//											4096

static const uint32_t SurfaceLodLineCount[MAX_SURFACE_CACHE_LOD] = {
	16, 16, 16, 16, 32
};

// 分配的SurfaceCache块的空间位置
struct SurfaceAtlasRange
{
	UVec2 offset = UVec2::Zero();
	UVec2 extent = UVec2::Zero();

	// 以下数据可以靠offset和extent反推出来，简单起见还是直接存吧
	uint32_t lod = 0;
	uint32_t line = 0;
	uint32_t blockOffset = 0;
	uint32_t blockCount = 0;

	UVec2 TiledOffset(uint32_t tileSize);	// 划分为tile后左上角的起始偏移值
	UVec2 TiledExtent(uint32_t tileSize);	// 划分为tile后的尺寸
	uint32_t AllocatedSize();
};
typedef std::shared_ptr<SurfaceAtlasRange> SurfaceAtlasRangeRef;

// 管理SurfaceCache块分配和释放
class SurfaceAtlas 
{
public:
	SurfaceAtlas();

	SurfaceAtlasRangeRef Allocate(UVec2 targetExtent);

	void Release(SurfaceAtlasRangeRef range);

private:
	struct UnusedAtlasBlock	// 水平一行的双向空闲链表，记录还未被使用的块的偏移和大小
	{
		uint32_t blockOffset = 0;	
		uint32_t blockCount = 0;
		UnusedAtlasBlock* previous = nullptr;
		UnusedAtlasBlock* next = nullptr;
	};
	std::vector<UnusedAtlasBlock*> blocks[MAX_SURFACE_CACHE_LOD];


	SurfaceAtlasRangeRef AllocateInternal(UVec2 targetExtent, uint32_t lod);

	void MergeBlock(UnusedAtlasBlock* block, uint32_t lod, uint32_t line);
	void CleanUpRange(SurfaceAtlasRangeRef range);
	UVec2 BlockOffset(UnusedAtlasBlock* block, uint32_t lod, uint32_t line);
	
};




