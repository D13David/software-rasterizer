#ifndef PJD_RENDER_STATS_H
#define PJD_RENDER_STATS_H

#include "common.h"

#include <atomic>

using AtomicInt = std::atomic<int>;

typedef struct RenderStats
{
    AtomicInt    TrianglesClipped;
    AtomicInt    TrianglesCulled;
    AtomicInt    ZeroAreaTris;
    AtomicInt    TrianglesRendered;
} RenderStats;

extern RenderStats Stats;

#if PJD_USE_RENDER_STATS
#   define RENDER_STATS_ADD(name, value) std::atomic_fetch_add_explicit(&Stats.name, (value), std::memory_order_relaxed);
#else
#   define RENDER_STATS_ADD(name, value)
#endif

void ResetRenderStats();
void DrawRenderStats(int posX, int posY);

#endif // PJD_RENDER_STATS_H