#include "render_stats.h"
#include "raster.h"

#if PJD_USE_RENDER_STATS

RenderStats Stats;

void ResetRenderStats()
{
    Stats.TrianglesClipped = 0;
    Stats.TrianglesCulled = 0;
    Stats.ZeroAreaTris = 0;
    Stats.TrianglesRendered = 0;
    Stats.TilesCulled = 0;
    Stats.TilesFull = 0;
    Stats.TilesPartial = 0;
}

void DrawRenderStats(int posX, int posY)
{
    int offsetY = posY;

    WriteString(Format("Tris Culled: %d", Stats.TrianglesCulled.load()), posX, offsetY), offsetY += 10;
    WriteString(Format("Tris Clipped: %d", Stats.TrianglesClipped.load()), posX, offsetY), offsetY += 10;
    WriteString(Format("Zero Area Tris: %d", Stats.ZeroAreaTris.load()), posX, offsetY), offsetY += 10;
    WriteString(Format("Tris Rendered: %d", Stats.TrianglesRendered.load()), posX, offsetY), offsetY += 10;
    WriteString(Format("Tiles (Culled): %d", Stats.TilesCulled.load()), posX, offsetY), offsetY += 10;
    WriteString(Format("Tiles (Full): %d", Stats.TilesFull.load()), posX, offsetY), offsetY += 10;
    WriteString(Format("Tiles (Partial): %d", Stats.TilesPartial.load()), posX, offsetY), offsetY += 10;
}

#endif // PJD_USE_RENDER_STATS