#include "render_stats.h"
#include "raster.h"
#include "font_render.h"

RenderStats Stats;

void ResetRenderStats()
{
    Stats.TrianglesClipped = 0;
    Stats.TrianglesCulled = 0;
    Stats.TrianglesRendered = 0;
}

void DrawRenderStats(int posX, int posY)
{
    int offsetY = posY;

    FntWriteString(Format("Tris Culled: %d", Stats.TrianglesCulled), posX, offsetY), offsetY += 10;
    FntWriteString(Format("Tris Clipped: %d", Stats.TrianglesClipped), posX, offsetY), offsetY += 10;
    FntWriteString(Format("Tris Rendered: %d", Stats.TrianglesRendered), posX, offsetY), offsetY += 10;
}