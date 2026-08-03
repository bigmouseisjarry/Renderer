#include "SurfaceCache.h"
#include "Core/Math/Math.h"
#include "Function/Global/Definations.h"
#include <cstdint>
#include <cstdio>
#include <memory>

UVec2 SurfaceAtlasRange::TiledOffset(uint32_t tileSize)
{
    uint32_t widthTiles = this->blockOffset * SURFACE_CACHE_LOD_SIZE(this->lod) / tileSize;  
    uint32_t heightTiles = this->line * SURFACE_CACHE_LOD_SIZE(this->lod) / tileSize;
    for(int i = 0; i < this->lod; i++) 
        heightTiles += SurfaceLodLineCount[i] * SURFACE_CACHE_LOD_SIZE(i) / tileSize;

    return UVec2(widthTiles, heightTiles);
}

UVec2 SurfaceAtlasRange::TiledExtent(uint32_t tileSize)
{
    return UVec2(this->blockCount * SURFACE_CACHE_LOD_SIZE(this->lod) / tileSize, SURFACE_CACHE_LOD_SIZE(this->lod) / tileSize);
}

uint32_t SurfaceAtlasRange::AllocatedSize()
{
    return SURFACE_CACHE_LOD_SIZE(this->lod) * SURFACE_CACHE_LOD_SIZE(this->lod) * this->blockCount;
}

SurfaceAtlas::SurfaceAtlas()
{
    for(int lod = 0; lod < MAX_SURFACE_CACHE_LOD; lod++)
    {
        uint32_t lineCount = SurfaceLodLineCount[lod];
        blocks[lod].resize(lineCount);
        for(int line = 0; line < lineCount; line++)
        {
            auto block = new UnusedAtlasBlock();
            block->blockOffset = 0;
            block->blockCount = SURFACE_CACHE_SIZE / SURFACE_CACHE_LOD_SIZE(lod);
            block->previous = nullptr;
            block->next = nullptr;

            blocks[lod][line] = block;
        }
    }
}

SurfaceAtlasRangeRef SurfaceAtlas::Allocate(UVec2 targetExtent)
{
    if(targetExtent[0] < targetExtent[1])
    {
        printf("SurfaceAtlasRange height should be smaller than width!\n");
        return nullptr;
    }
    if(targetExtent[1] < MIN_SURFACE_CACHE_LOD_SIZE)
    {
        //printf("SurfaceAtlasRange height is to small, stop allocation!\n");
        return nullptr;
    }

    while (targetExtent[1] > MAX_SURFACE_CACHE_LOD_SIZE) {
        targetExtent[0] /= 2;
        targetExtent[1] /= 2;
    }

    uint32_t targetLod = Math::CeilLogTwo(MAX_SURFACE_CACHE_LOD_SIZE) - Math::CeilLogTwo(targetExtent[1]);

    SurfaceAtlasRangeRef ret = AllocateInternal(targetExtent, targetLod);
    if(ret) return ret;

    printf("SurfaceAtlas lod [%d] cannot allocate new range, trying to allocate smaller one...\n", targetLod);
    for(int lod = targetLod + 1; lod < MAX_SURFACE_CACHE_LOD; lod++)
    {
        targetExtent[0] /= 2;
        targetExtent[1] /= 2;
        ret = AllocateInternal(targetExtent, lod);
        if(ret) return ret; 
    }
      
    printf("Fail to allocate SurfaceAtlasRange!\n");
    return nullptr;
}

SurfaceAtlasRangeRef SurfaceAtlas::AllocateInternal(UVec2 targetExtent, uint32_t lod)
{
    uint32_t targetBlockCount = (targetExtent[0] / SURFACE_CACHE_LOD_SIZE(lod));
    if(targetExtent[0] % SURFACE_CACHE_LOD_SIZE(lod) != 0) targetBlockCount += 1;

    uint32_t lineCount = SurfaceLodLineCount[lod];
    for(int line = 0; line < lineCount; line++)
    {
        UnusedAtlasBlock* block = blocks[lod][line];
        while(block)
        {
            if(block->blockCount >= targetBlockCount)   // 尺寸足够
            {
                SurfaceAtlasRangeRef allocate = std::make_shared<SurfaceAtlasRange>();
                allocate->offset = BlockOffset(block, lod, line);
                allocate->extent = targetExtent; 
                allocate->lod = lod;
                allocate->line = line;
                allocate->blockOffset = block->blockOffset;
                allocate->blockCount = targetBlockCount;

                if(block->blockCount == targetBlockCount) 
                {
                    if(block == blocks[lod][line]) 
                    {
                        blocks[lod][line] = block->next;
                        if(block->next) block->next->previous = nullptr;
                    }
                    else
                    {
                        if(block->previous) block->previous->next = block->next;
                        if(block->next)     block->next->previous = block->previous;
                    }
                    delete block;
                    return allocate;
                }
                else {
                    block->blockOffset  += targetBlockCount;
                    block->blockCount   -= targetBlockCount;
                    return allocate;
                }
            }
            block = block->next;
        }
    }

    return nullptr;
}

void SurfaceAtlas::Release(SurfaceAtlasRangeRef range)
{
    if(range->blockCount == 0) return;  // 无效

    auto block = new UnusedAtlasBlock();
    block->blockOffset = range->blockOffset;
    block->blockCount = range->blockCount;
    block->previous = nullptr;
    block->next = nullptr;

    uint32_t lod = range->lod;
    uint32_t line = range->line;
    UnusedAtlasBlock* iter = blocks[lod][line];
    if(iter)
    {
        while(true)
        {
            if(iter->blockOffset > range->blockOffset)  // 找到首个更靠后的块
            {
                if(iter->previous) 
                {
                    block->previous = iter->previous;
                    iter->previous->next = block;
                }
                else 
                {
                    iter->previous = block;
                    blocks[lod][line] = block;
                }
                
                block->next = iter;
                iter->previous = block;
                MergeBlock(block, lod, line);
                CleanUpRange(range);
                return;
            }
            else if(iter->next) iter = iter->next;  // 向后寻找
            else {
                iter->next = block;
                block->previous = iter;
                MergeBlock(block, lod, line);
                CleanUpRange(range);
                return;
            }
        }
    }
    else 
    {
        blocks[lod][line] = block;
        CleanUpRange(range);
    }
}

void SurfaceAtlas::MergeBlock(UnusedAtlasBlock* block, uint32_t lod, uint32_t line)
{
    UnusedAtlasBlock* previous = block->previous;
    UnusedAtlasBlock* next = block->next;

    // 每次合并最多只需考虑前一块和后一块
    if(previous && previous->blockOffset + previous->blockCount == block->blockOffset)
    {
        previous->blockCount += block->blockCount;

        previous->next = next;
        if(next) next->previous = previous;

        delete block;

        block = previous;
        previous = previous->previous;
    }
    
    if(next && block->blockOffset + block->blockCount == next->blockOffset)
    {
        next->blockOffset = block->blockOffset;
        next->blockCount += block->blockCount;

        next->previous = previous;
        if(previous) previous->next = next;
        else 
        {
            blocks[lod][line] = next;
            next->previous = nullptr;
        }    

        delete block;
    }
}

void SurfaceAtlas::CleanUpRange(SurfaceAtlasRangeRef range)
{
    range->offset = UVec2::Zero();
	range->extent = UVec2::Zero();
	range->lod = 0;
	range->line = 0;
	range->blockOffset = 0;
	range->blockCount = 0;
}

UVec2 SurfaceAtlas::BlockOffset(UnusedAtlasBlock* block, uint32_t lod, uint32_t line)
{
    UVec2 offset = UVec2::Zero();

    for(int i = 0; i < lod; i++) 
        offset[1] += SurfaceLodLineCount[i] * SURFACE_CACHE_LOD_SIZE(i);
    offset[1] += line * SURFACE_CACHE_LOD_SIZE(lod);

    offset[0] += block->blockOffset * SURFACE_CACHE_LOD_SIZE(lod);

    return offset;
}