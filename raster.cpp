#include "raster.h"
#include "mathlib_intrinsics.h"
#include "texture_loader.h"
#include "thread_pool.h"
#include "parallel_for.h"
#include "export_buffer.h"
#include "profile.h"
#include "render_stats.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <intrin.h>
#include <atomic>

using AtomicInt = std::atomic<int>;

// enable for rendering color coded mip-map levels
#define DEBUG_MIP_LEVELS 0

// enable for rendering screenspace derivatives accross polygons
#define DEBUG_SCREENSPACE_DERIVATIVES 0

#if DEBUG_MIP_LEVELS || DEBUG_SCREENSPACE_DERIVATIVES
#   define DEBUG_VIEW 1
#endif

#define ENABLE_CHECKERBOARD_RENDERING 0

#define FB_WIDTH    Ctx.Out.Width
#define FB_HEIGHT   Ctx.Out.Height

#define TILE_WIDTH          64
#define TILE_HEIGHT         16
#define TILE_COUNT_X        (FB_WIDTH / TILE_WIDTH)
#define TILE_COUNT_Y        (FB_HEIGHT / TILE_HEIGHT)
#define MAX_TRIS_PER_TILE   1024
#define MAX_TILES           ((1920 / TILE_WIDTH) * (1080 / TILE_HEIGHT))

#define THREAD_GROUP_SIZE  128
#define EXPORT_BUFFER_SIZE (32 * 1024 * 1024)

typedef struct RasterContext
{
    struct {
        int         Width;
        int         Height;
        void*       CB;
        void*       DB;
    } Out;
    TextureFilter   Filter;
    TextureView     Texture;
} RasterContext;

typedef struct ExportVertex
{
    int ScreenX;
    int ScreenY;
    float InvW;             // 1 / w
    float ZOverW;           // z / w
    float UOverW;           // u / w
    float VOverW;           // v / w
} ExportVertex;

typedef struct ScreenTile
{
    int BinnedTriangles[MAX_TRIS_PER_TILE];
    AtomicInt NumTriangles;
} ScreenTile;

typedef struct VertexTransformCommand
{
    const void*         Data;
    const uint16_t*     Indices;
    const InputElement* Elements;
    int                 NumInputElements;
    mat4                ProjectionMatrix;
} VertexTransformCommand;

static RasterContext Ctx;
static ExportBufferHandle ExportBuffer;
static ThreadPoolHandle ThreadPool;
static ScreenTile Tiles[MAX_TILES];
static uint32_t* ColorBuffer[2];
static int Frame;

void srInitialize(const RasterizerDesc& init)
{
    assert(init.FrameBufferPtr != NULL);

    Ctx.Out = {
        .Width = (int)init.BufferDesc.Width,
        .Height = (int)init.BufferDesc.Height,
        .CB = init.FrameBufferPtr,
        .DB = init.DepthBufferPtr
    };

    SYSTEM_INFO systemInfo;
    GetSystemInfo(&systemInfo);
#if 0
    int numThreads = 1;
#else
    int numThreads = systemInfo.dwNumberOfProcessors;
#endif
    ThreadPool = ThreadPoolInit(numThreads, 1024 * 32);

    size_t regionSize = THREAD_GROUP_SIZE * sizeof(ExportVertex) * 15; // upper bound for clipped vertices per region
    size_t maxRegions = (EXPORT_BUFFER_SIZE + regionSize - 1) / regionSize;
    ExportBuffer = ExportBufferCreate(EXPORT_BUFFER_SIZE, maxRegions);

#if ENABLE_CHECKERBOARD_RENDERING
    Frame = 0;
    for (int i = 0; i < 2; ++i) {
        ColorBuffer[i] = (uint32_t*)malloc(Ctx.Out.Width * Ctx.Out.Height * sizeof(uint32_t));
    }
#endif
}

void srDestroy()
{
    ThreadPoolDestroy(ThreadPool, ShutdownMode::IMMEDIATE);
}

void srSetTextureFilter(TextureFilter filter)
{
    Ctx.Filter = filter;
}

void srSetTextureView(TextureView texture)
{
    Ctx.Texture = texture;
}

void srClear(Color color)
{
    PROFILE_AUTO("Frame Buffer Clear");

    int bufferSize = FB_WIDTH * FB_HEIGHT;

#if ENABLE_CHECKERBOARD_RENDERING
    Color* colorBuffer = (Color*)ColorBuffer[Frame];
#else
    Color* colorBuffer = (Color*)Ctx.Out.CB;
#endif

    for (int i = 0; i < bufferSize; ++i) {
        colorBuffer[i] = color;
    } 

    float* depthBuffer = (float*)Ctx.Out.DB;
    for (int i = 0; i < bufferSize; ++i) {
        depthBuffer[i] = 1.0f;
    }
}

void srDrawPixel(int x, int y, Color color)
{
    assert(x < FB_WIDTH && y < FB_HEIGHT);
#if ENABLE_CHECKERBOARD_RENDERING
    ((Color*)ColorBuffer[Frame])[y * FB_WIDTH + x] = color;
#else
    ((Color*)Ctx.Out.CB)[y * FB_WIDTH + x] = color;
#endif
}

void srDrawLine(int x0, int y0, int x1, int y1, Color Color)
{
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    while (1)
    {
        srDrawPixel(x0, y0, Color);

        if (x0 == x1 && y0 == y1) {
            break;
        }

        int e2 = 2 * err;
        if (e2 > -dy)
        {
            err -= dy;
            x0 += sx;
        }

        if (e2 < dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void srDrawRectangle(int posX, int posY, int w, int h, Color color)
{
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            srDrawPixel(posX + x, posY + y, color);
        }
    }
}

static PJD_INLINE int Edge(int x0, int y0, int x1, int y1, int x2, int y2)
{
    return (y2 - y0) * (x1 - x0) - (x2 - x0) * (y1 - y0);
}

static PJD_INLINE void ComputeAABB(int x0, int y0, int x1, int y1, int x2, int y2, vec4i out)
{
    int minx = min(min(x0, x1), x2);
    int maxx = max(max(x0, x1), x2);
    int miny = min(min(y0, y1), y2);
    int maxy = max(max(y0, y1), y2);

    // Floor min to nearest multiple of 2, clamp to framebuffer
    int xmin = max(minx & ~1, 0);
    int ymin = max(miny & ~1, 0);

    // Ceil max to nearest multiple of 2, clamp to framebuffer
    int xmax = min((maxx + 1) & ~1, FB_WIDTH - 1);
    int ymax = min((maxy + 1) & ~1, FB_HEIGHT - 1);

    // Output
    out[0] = xmin;
    out[1] = ymin;
    out[2] = xmax;
    out[3] = ymax;
}

Color srSampleTextureLod(int sx, int sy, float u, float v, float mipLevel)
{
    TextureView texture = Ctx.Texture;

    if (texture.Data == NULL) {
        texture = LoadCheckerboardTexture();
    }

    if (Ctx.Filter == Unreal)
    {
        static constexpr float shift[2][4] =
        { 
            { -0.5f, -1.0f,  0.0f,  0.5f },
            { -1.0f,  0.0f,  0.5f, -0.5f } 
        };
        
        const int index = ((sx & 1) << 1) | (sy & 1);
        u += shift[0][index] / texture.Width;
        v += shift[1][index] / texture.Height;
    }

    u = u - floorf(u);
    v = v - floorf(v);

    int mip = (int)(mipLevel + 0.5);
    uint32_t mipWidth = max(1u, texture.Width >> mip);
    uint32_t mipHeight = max(1u, texture.Height >> mip);

    int x = (int)(u * mipWidth);
    int y = (int)(v * mipHeight);

    Color* mipData = (Color*)(((uint8_t*)texture.Data) + texture.MipOffsets[mip]);
    return mipData[y * mipWidth + x];
}

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
    Lerp(MipDebugColors[firstMipLevel + 0], MipDebugColors[firstMipLevel + 1], mipLevel - firstMipLevel, mipDebugColor);
    return COLOR(mipDebugColor[0], mipDebugColor[1], mipDebugColor[2]);
}
#endif

static void srDrawTriangle(const ExportVertex* v0, const ExportVertex* v1, const ExportVertex* v2, int tileIndex)
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

    float invArea = 1.0f / area;

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

    int A01 = y0 - y1, B01 = x1 - x0;
    int A12 = y1 - y2, B12 = x2 - x1;
    int A20 = y2 - y0, B20 = x0 - x2;

    int cy0 = Edge(x1, y1, x2, y2, bounds[0], bounds[1]);
    int cy1 = Edge(x2, y2, x0, y0, bounds[0], bounds[1]);
    int cy2 = Edge(x0, y0, x1, y1, bounds[0], bounds[1]);

    float* depthBuffer = (float*)Ctx.Out.DB;

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

            float w0[4], w1[4], w2[4];
            float zndc[4], u[4], v[4];
            int   px[4], py[4];

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
                    if (zndc[i] < depthBuffer[py[i] * FB_WIDTH + px[i]])
                    {
                        depthBuffer[py[i] * FB_WIDTH + px[i]] = zndc[i];
#if DEBUG_SCREENSPACE_DERIVATIVES
                        Color color = DebugViewScreenSpaceDerivatives(dudx, dudy, dvdx, dvdy);
#elif DEBUG_MIP_LEVELS
                        Color color = DebugViewMipLevel(mipLevel);
#else
                        Color color = srSampleTextureLod(px[i], py[i], u[i], v[i], mipLevel);
#endif
                        srDrawPixel(px[i], py[i], color);
                    }
                }
            }

            cx0Row += 2 * A12;
            cx1Row += 2 * A20;
            cx2Row += 2 * A01;
        }

        cy0 += 2 * B12;
        cy1 += 2 * B20;
        cy2 += 2 * B01;
    }
}

static PJD_INLINE uint8_t VertexOutcode(vec4 p)
{
    uint8_t outcode = 0;
    if (p[0] < -p[3]) outcode |= (1 << 0);
    if (p[0] >  p[3]) outcode |= (1 << 1);
    if (p[1] < -p[3]) outcode |= (1 << 2);
    if (p[1] >  p[3]) outcode |= (1 << 3);
    if (p[2] < -p[3]) outcode |= (1 << 4);
    if (p[2] >  p[3]) outcode |= (1 << 5);
    return outcode;
}

typedef struct ClipVertex
{
    vec4    ClipSpacePos;
    vec2    TexCoords;
} ClipVertex;

static ClipVertex Clip(const ClipVertex* v0, const ClipVertex* v1, float dot0, float dot1)
{
    ClipVertex out;
    float alpha = dot0 / (dot0 - dot1);
    LerpVec4(v0->ClipSpacePos, v1->ClipSpacePos, alpha, out.ClipSpacePos);
    LerpVec2(v0->TexCoords, v1->TexCoords, alpha, out.TexCoords);
    return out;
}

static int ClipPolygonAgainstPlane(const vec4 plane, ClipVertex* verts, int numVertices, ClipVertex* clippedVerts)
{
    assert(numVertices != 0);

    ClipVertex prev = verts[numVertices - 1];
    float prevDist = Vec4Dot(prev.ClipSpacePos, plane);
    int prevInside = prevDist >= 0.0f;
    int count = 0;

    for (int i = 0; i < numVertices; ++i)
    {
        ClipVertex curr = verts[i];
        float currDist = Vec4Dot(curr.ClipSpacePos, plane);
        int currInside = currDist >= 0.0f;

        if (currInside)
        {
            if (!prevInside) {
                clippedVerts[count++] = Clip(&prev, &curr, prevDist, currDist);
            }
            clippedVerts[count++] = curr;
        }
        else if (prevInside)
        {
            clippedVerts[count++] = Clip(&prev, &curr, prevDist, currDist);
        }

        prev = curr;
        prevInside = currInside;
        prevDist = currDist;
    }

    return count;
}

static int ClipTriangleAgainstFrustum(ClipVertex verts[3], uint8_t m0, uint8_t m1, uint8_t m2,  ClipVertex clippedVerts[12])
{
    // assume only intersecting triangles are processed here
    assert((m0 & m1 & m2) == 0 && (m0 | m1 | m2) != 0);

    static const vec4 FrustumPlanes[6] =
    {
        {  1,  0,  0,  1 }, // Left
        { -1,  0,  0,  1 }, // Right
        {  0,  1,  0,  1 }, // Bottom
        {  0, -1,  0,  1 }, // Top
        {  0,  0,  1,  1 }, // Near
        {  0,  0, -1,  1 }  // Far
    };

    ClipVertex buffer[2][12];

    memcpy(buffer[0], verts, sizeof(ClipVertex) * 3);

    int count = 3;
    int source = 0, target = 1;
    uint8_t mask = (m0 | m1 | m2);

    for (int i = 0; i < 6; ++i)
    {
        if (!(mask & (1 << i))) {
            continue;
        }

        count = ClipPolygonAgainstPlane(FrustumPlanes[i], buffer[source], count, buffer[target]);
        if (count == 0) {
            return 0;
        }

        // pingpong index update
        target = 1 - target, source = 1 - source;
    }

    memcpy(clippedVerts, buffer[source], sizeof(ClipVertex) * count);

    return count;
}

static void RunVertexTransform_(int start, int end, void* context)
{
    // setup vertex attribute streams
    VertexTransformCommand* command = (VertexTransformCommand*)context;
    int stride = srInputStreamElementSize(command->Elements, command->NumInputElements);

    const InputElement* inputElementPosition = srInputStreamElementByType(command->Elements, command->NumInputElements, InputElementType::TypePosition);
    assert(inputElementPosition != NULL);

    const InputElement* inputElementTexcoord = srInputStreamElementByType(command->Elements, command->NumInputElements, InputElementType::TypeTexcoord);
    assert(inputElementTexcoord != NULL);

    int index = start;
    int allocationHint = 0;

    do {
        // allocate a chunk of memory for our transformed vertex exports
        struct Range* range;
        int allocatedVertexCount = (allocationHint > 0 ? allocationHint : (end - index)) * 3;
        ExportVertex* exportVertexPtr = (ExportVertex*)ExportBufferReserve(ExportBuffer, allocatedVertexCount * sizeof(ExportVertex), NULL, &range);
        assert(exportVertexPtr != NULL);
        ExportVertex* exportVertexEndPtr = exportVertexPtr + allocatedVertexCount;

        allocationHint = 0;

        for ( ; index < end && exportVertexPtr < exportVertexEndPtr; ++index)
        {
            float* pos0 = (float*)srInputStreamElement(command->Data, *inputElementPosition, stride, command->Indices[index * 3 + 0]);
            float* pos1 = (float*)srInputStreamElement(command->Data, *inputElementPosition, stride, command->Indices[index * 3 + 1]);
            float* pos2 = (float*)srInputStreamElement(command->Data, *inputElementPosition, stride, command->Indices[index * 3 + 2]);

            float* tex[3];
            tex[0] = (float*)srInputStreamElement(command->Data, *inputElementTexcoord, stride, command->Indices[index * 3 + 0]);
            tex[1] = (float*)srInputStreamElement(command->Data, *inputElementTexcoord, stride, command->Indices[index * 3 + 1]);
            tex[2] = (float*)srInputStreamElement(command->Data, *inputElementTexcoord, stride, command->Indices[index * 3 + 2]);

            vec4 pos[3];
            Matrix4MulVec3(command->ProjectionMatrix, pos0, 1, pos[0]);
            Matrix4MulVec3(command->ProjectionMatrix, pos1, 1, pos[1]);
            Matrix4MulVec3(command->ProjectionMatrix, pos2, 1, pos[2]);

            // compute vertex outcode for trivial reject
            uint8_t out0 = VertexOutcode(pos[0]);
            uint8_t out1 = VertexOutcode(pos[1]);
            uint8_t out2 = VertexOutcode(pos[2]);

            // vertex is fully outside, we can skip it
            if ((out0 & out1 & out2) != 0)
            {
                RENDER_STATS_ADD(TrianglesCulled, 1);
                memset(exportVertexPtr, 0, sizeof(ExportVertex) * 3);
                exportVertexPtr += 3;
                continue;
            }

            ClipVertex input[3], clipped[12];
            int numClippedVertices = 0;
            if ((out0 | out1 | out2) != 0)
            {
                RENDER_STATS_ADD(TrianglesClipped, 1);

                // estimate upper bound of triangles after clipping and triangulating
                int maxVertices = (min(12, 3 + __popcnt(out0 | out1 | out2)) - 2) * 3;

                // not enough buffer space remaining to hold all triangles, start over
                ptrdiff_t numFreeVertices = exportVertexEndPtr - exportVertexPtr;
                if (numFreeVertices < maxVertices)
                {
                    allocationHint = maxVertices;
                    break;
                }

                // enough space left for export, then clip triangle
                for (int i = 0; i < 3; ++i)
                {
                    Vec4Copy(pos[i], input[i].ClipSpacePos);
                    Vec4Copy(tex[i], input[i].TexCoords);
                }

                numClippedVertices = ClipTriangleAgainstFrustum(input, out0, out1, out2, clipped);
            }
            else
            {
                // fully inside, no clipping needed
                for (int i = 0; i < 3; ++i)
                {
                    Vec4Copy(pos[i], clipped[i].ClipSpacePos);
                    Vec4Copy(tex[i], clipped[i].TexCoords);
                }
                numClippedVertices = 3;
            }

            for (int i = 1; i < numClippedVertices - 1; ++i)
            {
                ClipVertex v0 = clipped[0];
                ClipVertex v1 = clipped[i];
                ClipVertex v2 = clipped[i + 1];

                ClipToScreen(v0.ClipSpacePos, FB_WIDTH, FB_HEIGHT, v0.ClipSpacePos);
                ClipToScreen(v1.ClipSpacePos, FB_WIDTH, FB_HEIGHT, v1.ClipSpacePos);
                ClipToScreen(v2.ClipSpacePos, FB_WIDTH, FB_HEIGHT, v2.ClipSpacePos);

#define EXPORT_VERTEX(idx, vert) do {                                   \
        float invW = 1.0f / vert.ClipSpacePos[3];                       \
        exportVertexPtr[(idx)].ScreenX = (int)vert.ClipSpacePos[0];     \
        exportVertexPtr[(idx)].ScreenY = (int)vert.ClipSpacePos[1];     \
        exportVertexPtr[(idx)].InvW    = invW;                          \
        exportVertexPtr[(idx)].ZOverW  = vert.ClipSpacePos[2] * invW;   \
        exportVertexPtr[(idx)].UOverW  = vert.TexCoords[0] * invW;      \
        exportVertexPtr[(idx)].VOverW  = vert.TexCoords[1] * invW;      \
    } while (0)

                EXPORT_VERTEX(0, v0);
                EXPORT_VERTEX(1, v1);
                EXPORT_VERTEX(2, v2);

                exportVertexPtr += 3;

                RENDER_STATS_ADD(TrianglesRendered, 1);
            }
        }

        // publish the written range
        ExportBufferPublish(ExportBuffer, range);

    } while (index < end); // // previously allocated range is full but still vertices left to transform.. let's start over
}

static void RunRasterizeTriangles_(int tileIndexStart, int tileIndexEnd, void* context)
{
    for (int i = tileIndexStart; i < tileIndexEnd; ++i)
    {
        ScreenTile* tile = &Tiles[i];

        for (int j = 0; j < tile->NumTriangles; ++j)
        {
            ExportVertex* transformedVertices = (ExportVertex*)ExportBufferData(ExportBuffer) + tile->BinnedTriangles[j] * 3;
            srDrawTriangle(&transformedVertices[0], &transformedVertices[1], &transformedVertices[2], i);
        }
    }
}

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

static void RunVertexTransform(bool parallelize, int numPrimitives, VertexTransformCommand* command)
{
    PROFILE_AUTO("Vertex Transform");
    if (parallelize) ParallelFor(ThreadPool, 0, numPrimitives, THREAD_GROUP_SIZE, &RunVertexTransform_, command);
    else RunVertexTransform_(0, numPrimitives, command);
}

static void RunRasterizeTriangles(bool parallelize)
{
    PROFILE_AUTO("Rasterize");
    if (parallelize) ParallelFor(ThreadPool, 0, MAX_TILES, 1, &RunRasterizeTriangles_, NULL);
    else RunRasterizeTriangles_(0, MAX_TILES, NULL);
}

static void RunTriangleBinning(bool parallelize)
{
    PROFILE_AUTO("Triangle Binning");
    
    for (int i = 0; i < MAX_TILES; ++i) {
        Tiles[i].NumTriangles = 0;
    }

    int numTrianglesWritten = (int)(ExportBufferUsed(ExportBuffer) / sizeof(ExportVertex)) / 3;
    if (parallelize) ParallelFor(ThreadPool, 0, numTrianglesWritten, 16, &RunTriangleBinning_, NULL);
    else RunTriangleBinning_(0, numTrianglesWritten, NULL);
}

void srDrawTriangleList(const void* data, const uint16_t* indices, const InputElement* elements, int numInputElements, int numPrimitives, mat4 ProjectionMatrix, bool parallel)
{
    VertexTransformCommand command = {
        .Data = data,
        .Indices = indices,
        .Elements = elements,
        .NumInputElements = numInputElements
    };

    CopyMatrix(ProjectionMatrix, command.ProjectionMatrix);

    RunVertexTransform(parallel, numPrimitives, &command);

    RunTriangleBinning(parallel);

    RunRasterizeTriangles(parallel);

    // TODO: we probably don't need to reset the buffer here
    ExportBufferReset(ExportBuffer);
}

void srResolveFrameBuffer()
{
#if ENABLE_CHECKERBOARD_RENDERING
    Frame = 1 - Frame;

    Color* outCB = (Color*)Ctx.Out.CB;
    int width = Ctx.Out.Width;
    int height = Ctx.Out.Height;

    for (int y = 0; y < height; y += 2)
    {
        int yIndex0 = y * width;
        int yIndex1 = (y + 1) * width;

        for (int x = 0; x < width; x += 2)
        {
            outCB[yIndex0 + x] = ColorBuffer[0][yIndex0 + x];
            outCB[yIndex0 + x + 1] = ColorBuffer[1][yIndex0 + x + 1];
            outCB[yIndex1 + x] = ColorBuffer[1][yIndex1 + x];
            outCB[yIndex1 + x + 1] = ColorBuffer[0][yIndex1 + x + 1];
        }
    }
#endif 
}

static int FormatToSize(InputElementFormat format)
{
    switch (format)
    {
    case InputElementFormat::FormatRG32F:   return sizeof(float) * 2;
    case InputElementFormat::FormatRGB32F:  return sizeof(float) * 3;
    case InputElementFormat::FormatRGBA32F: return sizeof(float) * 4;
    }
    return -1;
}

int srInputStreamElementSize(const InputElement* elements, int numElements)
{
    assert(elements != NULL);
    assert(numElements >= 1);

    uint32_t maxOffset = elements[0].Offset;
    InputElementFormat lastElementFormat = elements[0].Format;

    for (int i = 1; i < numElements; ++i)
    {
        if (elements[i].Offset > maxOffset)
        {
            maxOffset = elements[i].Offset;
            lastElementFormat = elements[i].Format;
        }
    }

    return maxOffset + FormatToSize(lastElementFormat);
}

const InputElement* srInputStreamElementByType(const InputElement* elements, int numElements, InputElementType type)
{
    for (int i = 0; i < numElements; ++i)
    {
        if (elements[i].Type == type) {
            return &elements[i];
        }
    }
    return nullptr;
}

const void* srInputStreamElement(const void* stream, InputElement element, int stride, int index)
{
    const uint8_t* elementStart = (const uint8_t*)stream + element.Offset;
    return elementStart + index * stride;
}
