#include "raster.h"
#include "raster_internal.h"
#include "profile.h"
#include "thread_pool.h"
#include "parallel_for.h"
#include "export_buffer.h"
#include "render_stats.h"
#include "math.h"

#if DEBUG_SCREENSPACE_DERIVATIVES
static Color DebugViewScreenSpaceDerivatives(float dudx, float dudy, float dvdx, float dvdy)
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

static Color DebugViewMipLevel(float mipLevel)
{
    vec3 mipDebugColor;
    int firstMipLevel = (int)mipLevel;
    LerpVec3(MipDebugColors[firstMipLevel + 0], MipDebugColors[firstMipLevel + 1], mipLevel - firstMipLevel, mipDebugColor);
    return COLOR(mipDebugColor[0], mipDebugColor[1], mipDebugColor[2]);
}
#endif

static PJD_INLINE void RasterizeQuad(const ExportVertex* v0, const ExportVertex* v1, const ExportVertex* v2, int x, int y, int cx0[4], int cx1[4], int cx2[4], float invArea, int bufferOffset = 0)
{
    float w0[4], w1[4], w2[4];
    float zndc[4], u[4], v[4];
    int   px[4], py[4];

    float* depthBuffer = (float*)Ctx.Out.DB;

    // precompute interpolants for quad
    for (int i = 0; i < 4; ++i)
    {
        px[i] = x + (i & 1);
        py[i] = y + (i >> 1);

        w0[i] = cx0[i] * invArea;
        w1[i] = cx1[i] * invArea;
        w2[i] = cx2[i] * invArea;

        float depth = 1.0f / (w0[i] * v0->InvW + w1[i] * v1->InvW + w2[i] * v2->InvW);
        zndc[i] = (w0[i] * v0->ZOverW + w1[i] * v1->ZOverW + w2[i] * v2->ZOverW) * depth;
        u[i]    = (w0[i] * v0->UOverW + w1[i] * v1->UOverW + w2[i] * v2->UOverW) * depth;
        v[i]    = (w0[i] * v0->VOverW + w1[i] * v1->VOverW + w2[i] * v2->VOverW) * depth;
    }

    // calculate u/v derivatives and mip-level
    float dudx = ((u[1] + u[3]) - (u[0] + u[2])) * 0.5f;
    float dudy = ((u[2] + u[3]) - (u[0] + u[1])) * 0.5f;
    float dvdx = ((v[1] + v[3]) - (v[0] + v[2])) * 0.5f;
    float dvdy = ((v[2] + v[3]) - (v[0] + v[1])) * 0.5f;

    float dudx2 = dudx * dudx;
    float dudy2 = dudy * dudy;
    float dvdx2 = dvdx * dvdx;
    float dvdy2 = dvdy * dvdy;

    float rho2 = fmaxf(dudx2 + dvdx2, dudy2 + dvdy2);
    float mipLevel = 0.5f * Log2Fast(rho2 * Ctx.Texture.Width * Ctx.Texture.Width);
    mipLevel = Clamp(mipLevel, 0.0f, (float)Ctx.Texture.MipLevels - 1);

#if ENABLE_CHECKERBOARD_RENDERING
    for (int hi = 0; hi < 2; ++hi)
#else
    for (int i = 0; i < 4; ++i)
#endif
    {
#if ENABLE_CHECKERBOARD_RENDERING
        int i = (hi << 1) | (hi ^ Frame);
#endif
        if (cx0[i] >= 0 && cx1[i] >= 0 && cx2[i] >= 0)
        {
#if ENABLE_TILED_FRAMEBUFFER_LAYOUT
            int index = bufferOffset + i;
#else
            int index = py[i] * FB_WIDTH + px[i];
#endif
            if (zndc[i] < depthBuffer[index])
            {
                depthBuffer[index] = zndc[i];
#if DEBUG_SCREENSPACE_DERIVATIVES
                Color color = DebugViewScreenSpaceDerivatives(dudx, dudy, dvdx, dvdy);
#elif DEBUG_MIP_LEVELS
                Color color = DebugViewMipLevel(mipLevel);
#else
                Color color = SampleTextureLod(px[i], py[i], u[i], v[i], mipLevel);
#endif
                WriteFramebufferDirect(index, color);
            }
        }
    }
}

static void RasterizeQuadLinearEdgeIncrement(const ExportVertex* v0, const ExportVertex* v1, const ExportVertex* v2, vec4i bounds, float invArea)
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

    for (int y = bounds[1]; y <= bounds[3]; y += 2)
    {
        int cx0Row = cy0;
        int cx1Row = cy1;
        int cx2Row = cy2;

        for (int x = bounds[0]; x <= bounds[2]; x += 2)
        {
            int cx0[4] = { cx0Row, cx0Row + A12, cx0Row + B12, cx0Row + A12 + B12 };
            int cx1[4] = { cx1Row, cx1Row + A20, cx1Row + B20, cx1Row + A20 + B20 };
            int cx2[4] = { cx2Row, cx2Row + A01, cx2Row + B01, cx2Row + A01 + B01 };

            RasterizeQuad(v0, v1, v2, x, y, cx0, cx1, cx2, invArea);

            cx0Row += 2 * A12;
            cx1Row += 2 * A20;
            cx2Row += 2 * A01;
        }

        cy0 += 2 * B12;
        cy1 += 2 * B20;
        cy2 += 2 * B01;
    }
}

static void RasterizeQuadSwizzled(const ExportVertex* v0, const ExportVertex* v1, const ExportVertex* v2, vec4i bounds, float invArea, int tileIndex)
{
    int x0 = v0->ScreenX, y0 = v0->ScreenY;
    int x1 = v1->ScreenX, y1 = v1->ScreenY;
    int x2 = v2->ScreenX, y2 = v2->ScreenY;

    int tileMinX = (tileIndex % TILE_COUNT_X) * TILE_WIDTH;
    int tileMinY = (tileIndex / TILE_COUNT_X) * TILE_HEIGHT;

    for (int pos = 0; pos < TILE_WIDTH * TILE_HEIGHT; pos += 4)
    {
        int quadX = tileMinX + TileIndices[pos][0];
        int quadY = tileMinY + TileIndices[pos][1];

        if (quadX < bounds[0] || quadX > bounds[2] ||
            quadY < bounds[1] || quadY > bounds[3]) {
            continue;
        }

        int cx0[4], cx1[4], cx2[4];
        for (int i = 0; i < 4; ++i) 
        {
            int px = quadX + (i & 1);
            int py = quadY + (i >> 1);

            cx0[i] = Edge(x1, y1, x2, y2, px, py);
            cx1[i] = Edge(x2, y2, x0, y0, px, py);
            cx2[i] = Edge(x0, y0, x1, y1, px, py);
        }

        int tileOffset = tileIndex * (TILE_WIDTH * TILE_HEIGHT) + pos;

        RasterizeQuad(v0, v1, v2, quadX, quadY, cx0, cx1, cx2, invArea, tileOffset);
    }
}

static void DrawTriangle(const ExportVertex* v0, const ExportVertex* v1, const ExportVertex* v2, int tileIndex)
{
    int x0 = v0->ScreenX, y0 = v0->ScreenY;
    int x1 = v1->ScreenX, y1 = v1->ScreenY;
    int x2 = v2->ScreenX, y2 = v2->ScreenY;

    int area = Edge(x0, y0, x1, y1, x2, y2);

    // reject zero area triangles. vertex transform exports zero area triangles for fully discarted triangles
    if (area == 0) {
        RENDER_STATS_ADD(ZeroAreaTris, 1);
        return;
    }

    vec4i bounds;
    ComputeAABB(x0, y0, x1, y1, x2, y2, bounds);

    // clamp bounds to screen tile
    int tileMinX = (tileIndex % TILE_COUNT_X) * TILE_WIDTH;
    int tileMinY = (tileIndex / TILE_COUNT_X) * TILE_HEIGHT;
    int tileMaxX = min(tileMinX + TILE_WIDTH - 1, FB_WIDTH - 1);
    int tileMaxY = min(tileMinY + TILE_HEIGHT - 1, FB_HEIGHT - 1);

    bounds[0] = max(bounds[0], tileMinX);
    bounds[1] = max(bounds[1], tileMinY);
    bounds[2] = min(bounds[2], tileMaxX);
    bounds[3] = min(bounds[3], tileMaxY);

#if ENABLE_TILED_FRAMEBUFFER_LAYOUT
    RasterizeQuadSwizzled(v0, v1, v2, bounds, 1.0f / area, tileIndex);
#else
    RasterizeQuadLinearEdgeIncrement(v0, v1, v2, bounds, 1.0f / area);
#endif
}

static void RunRasterizeTriangles_(int tileIndexStart, int tileIndexEnd, void* context)
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
    if (parallelize) ParallelFor(ThreadPool, 0, MAX_TILES, 1, &RunRasterizeTriangles_, NULL);
    else RunRasterizeTriangles_(0, MAX_TILES, NULL);
}