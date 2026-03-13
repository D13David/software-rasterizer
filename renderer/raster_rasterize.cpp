#include "raster.h"
#include "raster_internal.h"
#include "common/profile.h"
#include "common/thread_pool.h"
#include "common/parallel_for.h"
#include "common/export_buffer.h"
#include "render_stats.h"

typedef enum TileCoverage : uint8_t
{
    TilePartialCoverage = 0,
    TileFullCoverage    = 1,
    TileNoCoverage      = 2,
} TileCoverage;

#if DEBUG_TILE_CLASSIFICATION
uint8_t* Coverage;
static void ResetCoverageBuffer()
{
    int numElements = TILE_COUNT_X * TILE_COUNT_Y;
    if (!Coverage) {
        Coverage = (uint8_t*)malloc(numElements * sizeof(uint8_t));
        assert(Coverage != NULL);
    }
    for (int i = 0; i < numElements; ++i) {
        Coverage[i] = 3;
    }
}
static void TouchTile(int tileIndex, TileCoverage coverage)
{
    if (!Coverage) {
        return;
    }

    if (coverage < Coverage[tileIndex]) {
        Coverage[tileIndex] = coverage;
    }
}
static void FillTileWithColor(int tileMinX, int tileMinY, int tileMaxX, int tileMaxY, rgba8 color)
{
    for (int y = tileMinY; y <= tileMaxY; ++y)
    {
        for (int x = tileMinX; x <= tileMaxX; ++x)
        {
            DrawPixel(x, y, color);
        }
    }
}
void DebugViewTileCoverage()
{
    if (!Coverage) 
    {
        ResetCoverageBuffer();
        return;
    }

    for (int tileIndex = 0; tileIndex < TILE_COUNT_X*TILE_COUNT_Y; ++tileIndex)
    {
        int tileOffset = tileIndex * (TILE_WIDTH * TILE_HEIGHT);
        int tileMinX = (tileIndex % TILE_COUNT_X) * TILE_WIDTH;
        int tileMinY = (tileIndex / TILE_COUNT_X) * TILE_HEIGHT;
        int tileMaxX = min(tileMinX + TILE_WIDTH - 1, FB_WIDTH - 1);
        int tileMaxY = min(tileMinY + TILE_HEIGHT - 1, FB_HEIGHT - 1);

        if (Coverage[tileIndex] == 3) {
            continue;
        }

        rgba8 color = COLOR(1, 0, 0);
        switch (Coverage[tileIndex])
        {
        case TilePartialCoverage: color = COLOR(0, 1, 0); break;
        case TileFullCoverage: color = COLOR(0, 0, 1); break;
        }
        FillTileWithColor(tileMinX, tileMinY, tileMaxX, tileMaxY, color);
    }

    ResetCoverageBuffer();
}
#endif

#if DEBUG_SCREENSPACE_DERIVATIVES
static rgba8 DebugViewScreenSpaceDerivatives(float dudx, float dudy, float dvdx, float dvdy)
{
    const float maxDeriv = 1.0f / 0.05f;
    float magU = sqrt(dudx * dudx + dudy * dudy);
    float magV = sqrt(dvdx * dvdx + dvdy * dvdy);
    float r = fmin(magU * maxDeriv, 1.0f);
    float g = fmin(magV * maxDeriv, 1.0f);
    return COLOR(r, g, 0.0f);
}
#endif

#if DEBUG_MIP_LEVELS
static const vec3 MipDebugColors[15] =
{
    {1.0f, 0.0f, 0.0f},     // red
    {0.0f, 1.0f, 0.0f},     // green
    {0.0f, 0.0f, 1.0f},     // blue
    {1.0f, 1.0f, 0.0f},     // yellow
    {1.0f, 0.0f, 1.0f},     // magenta
    {0.0f, 1.0f, 1.0f},     // cyan
    {1.0f, 0.5f, 0.0f},     // orange
    {0.5f, 0.0f, 1.0f},     // purple
    {0.0f, 0.5f, 1.0f},     // sky blue
    {0.5f, 1.0f, 0.0f},     // lime
    {1.0f, 0.0f, 0.5f},     // pink
    {0.0f, 0.5f, 0.0f},     // dark green
    {0.5f, 0.25f, 0.0f},    // brown
    {0.25f, 0.25f, 0.25f},  // gray
    {1.0f, 1.0f, 1.0f}      // white
};

static rgba8 DebugViewMipLevel(float mipLevel)
{
    vec3 mipDebugColor;
    int firstMipLevel = (int)mipLevel;
    LerpVec3(MipDebugColors[firstMipLevel + 0], MipDebugColors[firstMipLevel + 1], mipLevel - firstMipLevel, mipDebugColor);
    return COLOR(mipDebugColor[0], mipDebugColor[1], mipDebugColor[2]);
}
#endif

static TileCoverage ClassifyTile(int tileMinX, int tileMinY, int tileMaxX, int tileMaxY, int edges[3][3])
{
    int allInside = 1;
    int allOutside = 0;

#define TEST_EDGE(index) do {                           \
        int A = edges[index][0];                        \
        int B = edges[index][1];                        \
        int C = edges[index][2];                        \
                                                        \
        int e0 = A * tileMinX + B * tileMinY + C;       \
        int e1 = A * tileMaxX + B * tileMinY + C;       \
        int e2 = A * tileMinX + B * tileMaxY + C;       \
        int e3 = A * tileMaxX + B * tileMaxY + C;       \
                                                        \
        allInside &= min(e0, min3(e1, e2, e3)) >= 0;    \
        allOutside |= max(e0, max3(e1, e2, e3)) < 0;    \
    } while(0)

    TEST_EDGE(0);
    TEST_EDGE(1);
    TEST_EDGE(2);

    assert(!(allOutside && allInside));

    return (TileCoverage)((allOutside << 1) | allInside);
}

static PJD_INLINE rgba8 ShadePixel(float mipLevel, const Interpolants* interp DEBUG_VIEW_ONLY_ARG(DebugParams params))
{
#if PJD_DEBUG_VIEW_ENABLED
    switch (Ctx.DebugMode)
    {
#if DEBUG_MIP_LEVELS
    case DM_FaceMipMapLevel: return DebugViewMipLevel(mipLevel);
#endif

#if DEBUG_SCREENSPACE_DERIVATIVES
    case DM_FaceDerivatives: return DebugViewScreenSpaceDerivatives(params.dudx, params.dudy, params.dvdx, params.dvdy);
#endif 
    }
#endif

    return Ctx.PixelShader(mipLevel, interp);
}

template<bool EdgeTest>
static PJD_INLINE void RasterizeQuad(int x, int y, float mipLevel, const Interpolants interpolants[4] DEBUG_VIEW_ONLY_ARG(DebugParams params), int bufferOffset = 0)
{
    float* depthBuffer = (float*)Ctx.Out.DB;

#if ENABLE_CHECKERBOARD_RENDERING
    for (int hi = 0; hi < 2; ++hi)
#else
    for (int i = 0; i < 4; ++i)
#endif
    {
#if ENABLE_CHECKERBOARD_RENDERING
        int i = (hi << 1) | (hi ^ Frame);
#endif
        if constexpr (EdgeTest)
        {
            if ((I(i, cx0) | I(i, cx1) | I(i, cx2)) < 0)
                continue;
        }
#if ENABLE_TILED_FRAMEBUFFER_LAYOUT
        int index = bufferOffset + CoordToTileIndex[y][x] + i;
#else
        int index = I(i,py) * FB_WIDTH + I(i,px);
#endif
        if (I(i,z) < depthBuffer[index])
        {
            depthBuffer[index] = I(i,z);

            WriteFramebufferDirect(index, ShadePixel(mipLevel, &interpolants[i] DEBUG_VIEW_ONLY_ARG(params)));
        }
    }
}

template<bool fullyCovered>
static void RasterizeQuadLinearEdgeIncrement(const ExportVertex* v0, const ExportVertex* v1, const ExportVertex* v2, vec4i bounds, float invArea, int tileIndex)
{
    int x0 = v0->ScreenX, y0 = v0->ScreenY;
    int x1 = v1->ScreenX, y1 = v1->ScreenY;
    int x2 = v2->ScreenX, y2 = v2->ScreenY;

    int A01 = y0 - y1, B01 = x1 - x0;
    int A12 = y1 - y2, B12 = x2 - x1;
    int A20 = y2 - y0, B20 = x0 - x2;

    int cy0 = Edge(x1, y1, x2, y2, bounds[0], bounds[1]);
    int cy1 = Edge(x2, y2, x0, y0, bounds[0], bounds[1]);
    int cy2 = Edge(x0, y0, x1, y1, bounds[0], bounds[1]);

    int tileOffset = tileIndex * (TILE_WIDTH * TILE_HEIGHT);
    int tileMinX = (tileIndex % TILE_COUNT_X) * TILE_WIDTH;
    int tileMinY = (tileIndex / TILE_COUNT_X) * TILE_HEIGHT;

    for (int y = bounds[1]; y <= bounds[3]; y += 2)
    {
        int cx0Row = cy0;
        int cx1Row = cy1;
        int cx2Row = cy2;

        for (int x = bounds[0]; x <= bounds[2]; x += 2)
        {
            Interpolants interpolants[4];

            I(0, cx0) = cx0Row; I(1, cx0) = cx0Row + A12; I(2, cx0) = cx0Row + B12; I(3, cx0) = cx0Row + A12 + B12;
            I(0, cx1) = cx1Row; I(1, cx1) = cx1Row + A20; I(2, cx1) = cx1Row + B20; I(3, cx1) = cx1Row + A20 + B20;
            I(0, cx2) = cx2Row; I(1, cx2) = cx2Row + A01; I(2, cx2) = cx2Row + B01; I(3, cx2) = cx2Row + A01 + B01;

            // precompute interpolants for quad
            for (int i = 0; i < 4; ++i)
            {
                float w0 = I(i,cx0) * invArea;
                float w1 = I(i,cx1) * invArea;
                float w2 = I(i,cx2) * invArea;

                I(i,px) = x + (i & 1);
                I(i,py) = y + (i >> 1);

                I(i,z)      = (w0 * v0->Z + w1 * v1->Z + w2 * v2->Z);

                float depth = 1.0f / (w0 * v0->InvW + w1 * v1->InvW + w2 * v2->InvW);
                I(i,u)      = (w0 * v0->UOverW + w1 * v1->UOverW + w2 * v2->UOverW) * depth;
                I(i,v)      = (w0 * v0->VOverW + w1 * v1->VOverW + w2 * v2->VOverW) * depth;

                I(i, nx)    = (w0 * v0->Normal[0] + w1 * v1->Normal[0] + w2 * v2->Normal[0]);
                I(i, ny)    = (w0 * v0->Normal[1] + w1 * v1->Normal[1] + w2 * v2->Normal[1]);
                I(i, nz)    = (w0 * v0->Normal[2] + w1 * v1->Normal[2] + w2 * v2->Normal[2]);

                I(i, r)     = (w0 * v0->Color[0] + w1 * v1->Color[0] + w2 * v2->Color[0]);
                I(i, g)     = (w0 * v0->Color[1] + w1 * v1->Color[1] + w2 * v2->Color[1]);
                I(i, b)     = (w0 * v0->Color[2] + w1 * v1->Color[2] + w2 * v2->Color[2]);
            }

            // calculate u/v derivatives and mip-level
            float dudx = ((I(1,u) + I(3,u)) - (I(0,u) + I(2,u))) * 0.5f;
            float dudy = ((I(2,u) + I(3,u)) - (I(0,u) + I(1,u))) * 0.5f;
            float dvdx = ((I(1,v) + I(3,v)) - (I(0,v) + I(2,v))) * 0.5f;
            float dvdy = ((I(2,v) + I(3,v)) - (I(0,v) + I(1,v))) * 0.5f;

            float dudx2 = dudx * dudx;
            float dudy2 = dudy * dudy;
            float dvdx2 = dvdx * dvdx;
            float dvdy2 = dvdy * dvdy;

            float rho2 = fmaxf(dudx2 + dvdx2, dudy2 + dvdy2);
            float mipLevel = 0.5f * Log2Fast(rho2 * Ctx.Texture.Width * Ctx.Texture.Width);
            mipLevel = Clamp(mipLevel, 0.0f, (float)Ctx.Texture.MipLevels - 1);

#if ENABLE_TILED_FRAMEBUFFER_LAYOUT
            int posX = x - tileMinX;
            int posY = y - tileMinY;
#else
            int posX = x;
            int posY = y;
#endif

#if PJD_DEBUG_VIEW_ENABLED
            DebugParams debugParams = { dudx, dudy, dvdx, dvdy };
#endif

            RasterizeQuad<!fullyCovered>(posX, posY, mipLevel, interpolants DEBUG_VIEW_ONLY_ARG(debugParams), tileOffset);

            cx0Row += 2 * A12;
            cx1Row += 2 * A20;
            cx2Row += 2 * A01;
        }

        cy0 += 2 * B12;
        cy1 += 2 * B20;
        cy2 += 2 * B01;
    }
}

static void DrawTriangle(const ExportVertex* v0, const ExportVertex* v1, const ExportVertex* v2, int tileIndex)
{
    int x0 = v0->ScreenX, y0 = v0->ScreenY;
    int x1 = v1->ScreenX, y1 = v1->ScreenY;
    int x2 = v2->ScreenX, y2 = v2->ScreenY;

    int area = Edge(x0, y0, x1, y1, x2, y2);

    // reject zero area triangles. vertex transform exports zero area triangles for fully discarted triangles
    if (area == 0) 
    {
        RENDER_STATS_ADD(ZeroAreaTris, 1);
        return;
    }

    // clamp bounds to screen tile
    int tileMinX = (tileIndex % TILE_COUNT_X) * TILE_WIDTH;
    int tileMinY = (tileIndex / TILE_COUNT_X) * TILE_HEIGHT;
    int tileMaxX = min(tileMinX + TILE_WIDTH - 1, FB_WIDTH - 1);
    int tileMaxY = min(tileMinY + TILE_HEIGHT - 1, FB_HEIGHT - 1);

    int edges[3][3] = {
        {y1 - y2, x2 - x1, x1 * y2 - y1 * x2},
        {y2 - y0, x0 - x2, x2 * y0 - y2 * x0},
        {y0 - y1, x1 - x0, x0 * y1 - y0 * x1}
    };

    TileCoverage coverage = ClassifyTile(tileMinX, tileMinY, tileMaxX, tileMaxY, edges);

#if DEBUG_TILE_CLASSIFICATION
    TouchTile(tileIndex, coverage);
#endif

    if (coverage == TileNoCoverage)
    {
        RENDER_STATS_ADD(TilesCulled, 1);
        return;
    }

    vec4i bounds;
    ComputeAABB(x0, y0, x1, y1, x2, y2, bounds);

    bounds[0] = max(bounds[0], tileMinX);
    bounds[1] = max(bounds[1], tileMinY);
    bounds[2] = min(bounds[2], tileMaxX);
    bounds[3] = min(bounds[3], tileMaxY);

    if (coverage == TileFullCoverage)
    {
        RENDER_STATS_ADD(TilesFull, 1);
        RasterizeQuadLinearEdgeIncrement<true>(v0, v1, v2, bounds, 1.0f / area, tileIndex);
    }
    else
    {
        RENDER_STATS_ADD(TilesPartial, 1);
        RasterizeQuadLinearEdgeIncrement<false>(v0, v1, v2, bounds, 1.0f / area, tileIndex);
    }
}

static void RunRasterizeTriangles_(size_t id, int tileIndexStart, int tileIndexEnd, void* context)
{
    for (int i = tileIndexStart; i < tileIndexEnd; ++i)
    {
        ScreenTile* tile = &Tiles[i];

        for (int j = 0; j < tile->NumTriangles; ++j)
        {
            ExportVertex* transformedVertices = (ExportVertex*)ExportBufferData(ExportBuffer) + tile->BinnedTriangles[j] * 3;
            DrawTriangle(&transformedVertices[0], &transformedVertices[1], &transformedVertices[2], i);
        }
    }
}

void RunRasterizeTriangles(bool parallelize)
{
    PROFILE_AUTO("Rasterize");
    if (parallelize) ParallelFor(ThreadPool, 0, MAX_TILES, 64, &RunRasterizeTriangles_, NULL);
    else RunRasterizeTriangles_(0, 0, MAX_TILES, NULL);
}