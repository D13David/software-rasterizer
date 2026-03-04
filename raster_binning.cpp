#include "raster.h"
#include "raster_internal.h"
#include "export_buffer.h"
#include "profile.h"
#include "thread_pool.h"
#include "parallel_for.h"

ScreenTile Tiles[MAX_TILES];

static void RunTriangleBinning_(int indexStart, int indexEnd, void* context)
{
    ExportVertex* transformedVertices = (ExportVertex*)ExportBufferData(ExportBuffer);

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
                ScreenTile* tile = &Tiles[tileIndex];
                int index = std::atomic_fetch_add_explicit(&tile->NumTriangles, 1, std::memory_order_relaxed);
                assert(index < MAX_TRIS_PER_TILE);
                tile->BinnedTriangles[index] = i;
            }
        }
    }
}

void RunTriangleBinning(bool parallelize)
{
    PROFILE_AUTO("Triangle Binning");

    for (int i = 0; i < MAX_TILES; ++i) {
        Tiles[i].NumTriangles = 0;
    }

    int numTrianglesWritten = (int)(ExportBufferUsed(ExportBuffer) / sizeof(ExportVertex)) / 3;
    if (parallelize) ParallelFor(ThreadPool, 0, numTrianglesWritten, 16, &RunTriangleBinning_, NULL);
    else RunTriangleBinning_(0, numTrianglesWritten, NULL);
}