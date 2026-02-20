#ifndef PJD_RENDER_STATS_H
#define PJD_RENDER_STATS_H

#include "common.h"

typedef struct RenderStats
{
    uint32_t    TrianglesClipped;
    uint32_t    TrianglesCulled;
    uint32_t    TrianglesRendered;
} RenderStats;

extern RenderStats Stats;

#if PJD_USE_RENDER_STATS
#   define RENDER_STATS_ADD(name, value) Stats.name += (value)
#else
#   define RENDER_STATS_ADD(name, value)
#endif

void ResetRenderStats();
void DrawRenderStats(int posX, int posY);

#endif // PJD_RENDER_STATS_H