#include "raster.h"
#include "raster_internal.h"
#include "export_buffer.h"
#include "profile.h"
#include "thread_pool.h"
#include "parallel_for.h"

#include <algorithm>

#define LOCAL_TILE_CACHE_SIZE 128
#define MAX_BINNING_WORKERS   16

typedef struct BinnedTriangle
{
    uint16_t tile;
    uint32_t index;
} BinnedTriangle;

typedef PJD_ALIGN(16) struct ThreadLocalTileCache
{
    BinnedTriangle Triangles[LOCAL_TILE_CACHE_SIZE];
    int            NumBinnedTriangles;
} ThreadLocalTileCache;

ThreadLocalTileCache  ThreadTileCache[MAX_BINNING_WORKERS];
ScreenTile            Tiles[MAX_TILES];

static int CompareCacheEntry(const void* a, const void* b)
{
    return ((const BinnedTriangle*)a)->tile - ((const BinnedTriangle*)b)->tile;
}

template<bool Synchronized>
static PJD_INLINE void FlushTileCache(ThreadLocalTileCache* cache)
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

        ScreenTile* tile = &Tiles[tileIndex];

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

static void RunTriangleBinning_(size_t id, int indexStart, int indexEnd, void* context)
{
    ExportVertex* transformedVertices = (ExportVertex*)ExportBufferData(ExportBuffer);

    ThreadLocalTileCache* tileCache = &ThreadTileCache[id];

    for (int i = indexStart; i < indexEnd; ++i)
    {
        const ExportVertex& v0 = transformedVertices[i * 3 + 0];
        const ExportVertex& v1 = transformedVertices[i * 3 + 1];
        const ExportVertex& v2 = transformedVertices[i * 3 + 2];

        int x0 = v0.ScreenX, y0 = v0.ScreenY;
        int x1 = v1.ScreenX, y1 = v1.ScreenY;
        int x2 = v2.ScreenX, y2 = v2.ScreenY;

        // triangle is backface culled
        int area = Edge(x0, y0, x1, y1, x2, y2);
        if (area < 0) {
            continue;
        }

        vec4i bounds;
        ComputeAABB(x0, y0, x1, y1, x2, y2, bounds);

        int minTileX = max(0, bounds[0] / TILE_WIDTH);
        int minTileY = max(0, bounds[1] / TILE_HEIGHT);
        int maxTileX = min(TILE_COUNT_X - 1, bounds[2] / TILE_WIDTH);
        int maxTileY = min(TILE_COUNT_Y - 1, bounds[3] / TILE_HEIGHT);

        for (int y = minTileY; y <= maxTileY; ++y)
        {
            for (int x = minTileX; x <= maxTileX; ++x)
            {
                int tileIndex = y * TILE_COUNT_X + x;

                assert(tileCache->NumBinnedTriangles < LOCAL_TILE_CACHE_SIZE);
                tileCache->Triangles[tileCache->NumBinnedTriangles].index = i;
                tileCache->Triangles[tileCache->NumBinnedTriangles].tile = tileIndex;
                tileCache->NumBinnedTriangles++;

                [[unlikely]]
                if (tileCache->NumBinnedTriangles >= LOCAL_TILE_CACHE_SIZE) {
                    FlushTileCache<false>(tileCache);
                }
            }
        }
    }
}

void RunTriangleBinning(bool parallelize)
{
    PROFILE_AUTO("Triangle Binning");

    assert(MAX_BINNING_WORKERS >= ThreadPoolGetNumWorkers(ThreadPool));

    for (int i = 0; i < MAX_TILES; ++i) {
        Tiles[i].NumTriangles = 0;
    }

    int numTrianglesWritten = (int)(ExportBufferUsed(ExportBuffer) / sizeof(ExportVertex)) / 3;
    if (parallelize) ParallelFor(ThreadPool, 0, numTrianglesWritten, THREAD_GROUP_SIZE_BINNING, &RunTriangleBinning_, NULL);
    else RunTriangleBinning_(0, 0, numTrianglesWritten, NULL);

    // flush remaining cache entries
    for (int tid = 0; tid < ThreadPoolGetNumWorkers(ThreadPool); ++tid) {
        FlushTileCache<true>(&ThreadTileCache[tid]);
    }
}