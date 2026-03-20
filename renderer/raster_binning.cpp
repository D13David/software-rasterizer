#include "raster.h"
#include "raster_internal.h"
#include "common/export_buffer.h"
#include "common/profile.h"
#include "common/thread_pool.h"
#include "common/parallel_for.h"

#include <algorithm>

#define LOCAL_TILE_CACHE_SIZE 128
#define MAX_BINNING_WORKERS   16

typedef struct BinnedTriangle
{
    uint16_t tile;
    uint32_t index;
} BinnedTriangle;

typedef struct ThreadLocalTileCache
{
    BinnedTriangle Triangles[LOCAL_TILE_CACHE_SIZE];
    int            NumBinnedTriangles;
} ThreadLocalTileCache;

ThreadLocalTileCache  ThreadTileCache[MAX_BINNING_WORKERS];
ScreenTile            TileBins[MAX_TILES];
ScreenTile            MacroTileBins[MAX_MACRO_TILES];

static int CompareCacheEntry(const void* a, const void* b)
{
    return ((const BinnedTriangle*)a)->tile - ((const BinnedTriangle*)b)->tile;
}

template<bool Synchronized>
static PJD_INLINE void FlushTileCache(ThreadLocalTileCache* cache, ScreenTile* tileBins)
{
    qsort(cache->Triangles, cache->NumBinnedTriangles, sizeof(BinnedTriangle), CompareCacheEntry);

    for (int i = 0; i < cache->NumBinnedTriangles; )
    {
        int tileIndex = cache->Triangles[i].tile;
        int start = i;

        while (i < cache->NumBinnedTriangles && cache->Triangles[i].tile == tileIndex) {
            i++;
        }

        int count = i - start;

        ScreenTile* tile = &tileBins[tileIndex];

        int base;
        if constexpr (Synchronized == false) {
            base = std::atomic_fetch_add_explicit(
                &tile->NumTriangles,
                count,
                std::memory_order_relaxed);
        } 
        else {
            base = tile->NumTriangles;
            tile->NumTriangles = base + count;
        }

        assert(base + count < MAX_TRIS_PER_TILE);

        for (int j = 0; j < count; j++) {
            tile->BinnedTriangles[base + j] = cache->Triangles[start + j].index;
        }
    }

    cache->NumBinnedTriangles = 0;
}

inline static bool IsTileOutsideTriangle(int tx0, int ty0, int tx1, int ty1, int tx2, int ty2,
    int tileMinX, int tileMinY, int tileMaxX, int tileMaxY)
{
    int e0_0 = Edge(tx0, ty0, tx1, ty1, tileMinX, tileMinY);
    int e0_1 = Edge(tx0, ty0, tx1, ty1, tileMaxX, tileMinY);
    int e0_2 = Edge(tx0, ty0, tx1, ty1, tileMinX, tileMaxY);
    int e0_3 = Edge(tx0, ty0, tx1, ty1, tileMaxX, tileMaxY);
    if (e0_0 < 0 && e0_1 < 0 && e0_2 < 0 && e0_3 < 0) return true;

    int e1_0 = Edge(tx1, ty1, tx2, ty2, tileMinX, tileMinY);
    int e1_1 = Edge(tx1, ty1, tx2, ty2, tileMaxX, tileMinY);
    int e1_2 = Edge(tx1, ty1, tx2, ty2, tileMinX, tileMaxY);
    int e1_3 = Edge(tx1, ty1, tx2, ty2, tileMaxX, tileMaxY);
    if (e1_0 < 0 && e1_1 < 0 && e1_2 < 0 && e1_3 < 0) return true;

    int e2_0 = Edge(tx2, ty2, tx0, ty0, tileMinX, tileMinY);
    int e2_1 = Edge(tx2, ty2, tx0, ty0, tileMaxX, tileMinY);
    int e2_2 = Edge(tx2, ty2, tx0, ty0, tileMinX, tileMaxY);
    int e2_3 = Edge(tx2, ty2, tx0, ty0, tileMaxX, tileMaxY);
    if (e2_0 < 0 && e2_1 < 0 && e2_2 < 0 && e2_3 < 0) return true;

    return false;
}

template<int TileWidth, int TileHeight>
static void BinTriangle(
    ThreadLocalTileCache* tileCache,
    const ExportVertex& v0,
    const ExportVertex& v1,
    const ExportVertex& v2,
    int triangleIndex,
    ScreenTile* tileBins,
    int tileGridWidth,
    int clipMinX, int clipMinY,
    int clipMaxX, int clipMaxY)
{
    int x0 = TO_FP28_4(v0.ScreenX);
    int y0 = TO_FP28_4(v0.ScreenY);
    int x1 = TO_FP28_4(v1.ScreenX);
    int y1 = TO_FP28_4(v1.ScreenY);
    int x2 = TO_FP28_4(v2.ScreenX);
    int y2 = TO_FP28_4(v2.ScreenY);

    int area = Edge(x0, y0, x1, y1, x2, y2);
    if (area <= 0) return;

    vec4i bounds;
    ComputeAABB(x0, y0, x1, y1, x2, y2, bounds);

    int minX = max(bounds[0], clipMinX);
    int minY = max(bounds[1], clipMinY);
    int maxX = min(bounds[2], clipMaxX);
    int maxY = min(bounds[3], clipMaxY);

    if (minX > maxX || minY > maxY) return;

    int minTileX = (minX - clipMinX) / TileWidth;
    int minTileY = (minY - clipMinY) / TileHeight;
    int maxTileX = (maxX - clipMinX) / TileWidth;
    int maxTileY = (maxY - clipMinY) / TileHeight;

    /* TODO
    * Large triangles
    *    I Bin into global bucket (each tile rasterizer checks global bucket and overlap)
    *   II Edge stepping for binning
    * 
    * Medium triangles
    *   Bin into tile buckets
    *   Reject empty tiles (edge test, or edge stepping)
    * 
    * Tiny triangles
    *    Bin into tile buckets
    *     => Prefix Sum with Scratch Allocator (2 phase to fix scattered writes (??)) or Fixed Buffer
    */

    for (int ty = minTileY; ty <= maxTileY; ++ty)
    {
        for (int tx = minTileX; tx <= maxTileX; ++tx)
        {
            int tileMinX = TO_FP28_4(clipMinX + tx * TileWidth);
            int tileMinY = TO_FP28_4(clipMinY + ty * TileHeight);
            int tileMaxX = TO_FP28_4(tileMinX + TileWidth);
            int tileMaxY = TO_FP28_4(tileMinY + TileHeight);

            if (IsTileOutsideTriangle(x0, y0, x1, y1, x2, y2, tileMinX, tileMinY, tileMaxX, tileMaxY)) {
                continue;
            }

            int tileIndex = (ty + clipMinY / TileHeight) * tileGridWidth + (tx + clipMinX / TileWidth);

            assert(tileCache->NumBinnedTriangles < LOCAL_TILE_CACHE_SIZE);
            tileCache->Triangles[tileCache->NumBinnedTriangles].index = triangleIndex;
            tileCache->Triangles[tileCache->NumBinnedTriangles].tile = tileIndex;
            tileCache->NumBinnedTriangles++;

            if (tileCache->NumBinnedTriangles >= LOCAL_TILE_CACHE_SIZE) {
                FlushTileCache<false>(tileCache, tileBins);
            }
        }
    }
}

static void RunTriangleBinning_(size_t id, int indexStart, int indexEnd, void* context)
{
    const Range* range = (const Range*)context;
    ExportVertex* transformedVertices = (ExportVertex*)range->Ptr;

    ThreadLocalTileCache* tileCache = &ThreadTileCache[id];

    for (int i = indexStart; i < indexEnd; ++i)
    {
        const ExportVertex& v0 = transformedVertices[i * 3 + 0];
        const ExportVertex& v1 = transformedVertices[i * 3 + 1];
        const ExportVertex& v2 = transformedVertices[i * 3 + 2];

        BinTriangle<MACRO_TILE_WIDTH, MACRO_TILE_HEIGHT>(
            tileCache,
            v0, v1, v2,
            i,
            MacroTileBins,
            MACRO_TILE_COUNT_X,
            0, 0,
            FB_WIDTH - 1, FB_HEIGHT - 1
        );
    }
    FlushTileCache<false>(tileCache, MacroTileBins);
}

static void RunTriangleBinningPhase2_(size_t id, int indexStart, int indexEnd, void* context)
{
    // tasks runs on one macro tile each
    assert(indexEnd - indexStart == 1);

    const Range* range = (const Range*)context;
    ExportVertex* transformedVertices = (ExportVertex*)range->Ptr;

    ScreenTile& macroTile = MacroTileBins[indexStart];
    ThreadLocalTileCache* tileCache = &ThreadTileCache[id];

    int macroX = indexStart % MACRO_TILE_COUNT_X;
    int macroY = indexStart / MACRO_TILE_COUNT_X;

    int macroMinX = macroX * MACRO_TILE_WIDTH;
    int macroMinY = macroY * MACRO_TILE_HEIGHT;
    int macroMaxX = macroMinX + MACRO_TILE_WIDTH - 1;
    int macroMaxY = macroMinY + MACRO_TILE_HEIGHT - 1;

    for (int i = 0; i < macroTile.NumTriangles; ++i)
    {
        int triangleIndex = macroTile.BinnedTriangles[i];

        const ExportVertex& v0 = transformedVertices[triangleIndex * 3 + 0];
        const ExportVertex& v1 = transformedVertices[triangleIndex * 3 + 1];
        const ExportVertex& v2 = transformedVertices[triangleIndex * 3 + 2];

        BinTriangle<TILE_WIDTH, TILE_HEIGHT>(
            tileCache,
            v0, v1, v2,
            triangleIndex,
            TileBins,
            TILE_COUNT_X,
            macroMinX, macroMinY,
            macroMinX + MACRO_TILE_WIDTH - 1,
            macroMinY + MACRO_TILE_HEIGHT - 1
        );
    }
    FlushTileCache<false>(tileCache, TileBins);
}

void RunTriangleBinning(bool parallelize, const Range* range)
{
    PROFILE_AUTO("Triangle Binning");

    assert(MAX_BINNING_WORKERS >= ThreadPoolGetNumWorkers(ThreadPool));

    for (int i = 0; i < MAX_MACRO_TILES; ++i) {
        MacroTileBins[i].NumTriangles = 0;
    }
    for (int i = 0; i < MAX_TILES; ++i) {
        TileBins[i].NumTriangles = 0;
    }

    int numTrianglesWritten = (range->Size / sizeof(ExportVertex)) / 3;
    if (parallelize) ParallelFor(ThreadPool, 0, numTrianglesWritten, THREAD_GROUP_SIZE_BINNING, &RunTriangleBinning_, true, (void*)range);
    else RunTriangleBinning_(0, 0, numTrianglesWritten, (void*)range);

    if (parallelize) ParallelFor(ThreadPool, 0, MAX_MACRO_TILES, 1, &RunTriangleBinningPhase2_, true, (void*)range);
    else RunTriangleBinningPhase2_(0, 0, MAX_MACRO_TILES, (void*)range);
}