#ifndef PJD_RENDER_STATS_H
#define PJD_RENDER_STATS_H

#include "common.h"

#include <atomic>

using AtomicInt = std::atomic<int>;

typedef struct RenderStats
{
    PJD_ALIGN(16) AtomicInt TrianglesClipped;
    PJD_ALIGN(16) AtomicInt TrianglesCulled;
    PJD_ALIGN(16) AtomicInt ZeroAreaTris;
    PJD_ALIGN(16) AtomicInt TrianglesRendered;
    PJD_ALIGN(16) AtomicInt TilesCulled;
    PJD_ALIGN(16) AtomicInt TilesFull;
    PJD_ALIGN(16) AtomicInt TilesPartial;
} RenderStats;

extern RenderStats Stats;

#if PJD_USE_RENDER_STATS
#   define RENDER_STATS_ADD(name, value) do { std::atomic_fetch_add_explicit(&Stats.name, (value), std::memory_order_relaxed); } while(0)
#else
#   define RENDER_STATS_ADD(name, value)
#endif

#if PJD_USE_RENDER_STATS
void ResetRenderStats();
void DrawRenderStats(int posX, int posY);
#endif // PJD_USE_RENDER_STATS

#endif // PJD_RENDER_STATS_H