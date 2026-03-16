#ifndef PJD_RASTER_INTERNAL_H
#define PJD_RASTER_INTERNAL_H

#include <atomic>

using AtomicInt = std::atomic<int>;

// enable checkerboard rendering (prototype)
#define ENABLE_CHECKERBOARD_RENDERING 0

// enable tiled framebuffer layout
#define ENABLE_TILED_FRAMEBUFFER_LAYOUT 1

#if PJD_DEBUG_VIEW_ENABLED
    // enable for rendering color coded mip-map levels
    #define DEBUG_MIP_LEVELS 1

    // enable for rendering screenspace derivatives accross polygons
    #define DEBUG_SCREENSPACE_DERIVATIVES 1

    // enable tile classification drawing
    #define DEBUG_TILE_CLASSIFICATION 1
#else
    #define DEBUG_MIP_LEVELS 0
    #define DEBUG_SCREENSPACE_DERIVATIVES 0
    #define DEBUG_TILE_CLASSIFICATION 0
#endif

#if ENABLE_TILED_FRAMEBUFFER_LAYOUT && ENABLE_CHECKERBOARD_RENDERING
#   error "nope"
#endif

#define FB_WIDTH    Ctx.Out.Width
#define FB_HEIGHT   Ctx.Out.Height

#if ENABLE_TILED_FRAMEBUFFER_LAYOUT
#define TILE_WIDTH          16
#define TILE_HEIGHT         16
#else
#define TILE_WIDTH          64
#define TILE_HEIGHT         16
#endif // ENABLE_TILED_FRAMEBUFFER_LAYOUT
#define TILE_COUNT_X        (FB_WIDTH / TILE_WIDTH)
#define TILE_COUNT_Y        (FB_HEIGHT / TILE_HEIGHT)
#define MAX_TRIS_PER_TILE   1024*128
#define MAX_TILES           ((1920 / TILE_WIDTH) * (1080 / TILE_HEIGHT))

#define THREAD_GROUP_SIZE_BINNING      32
#define THREAD_GROUP_SIZE_VTRANSFORM  128
#define THREAD_GROUP_SIZE_RASTERIZE    16

typedef struct RasterContext
{
    struct {
        int         Width;
        int         Height;
        void* CB;
        void* DB;
    } Out;
    PS              PixelShader;
    TextureFilter   Filter;
    TextureView     Texture;
    DrawMode        DrawMode;
    bool            DepthWriteEnabled;
#if PJD_DEBUG_VIEW_ENABLED
    DebugMode       DebugMode;
#endif
} RasterContext;

typedef struct ScreenTile
{
    int* BinnedTriangles;
    PJD_ALIGN(64) AtomicInt NumTriangles;
} ScreenTile;

typedef struct PJD_ALIGN(64) ExportVertex
{
    uint16_t ScreenX, ScreenY; //           4 byte
    float Z;                   // z         4 byte
    float InvW;                // 1 / w     4 byte
    float UOverW;              // u / w     4 byte
    float VOverW;              // v / w     4 byte
    vec4  Color;               // rgbX     12 byte
    vec4  Normal;              // xyzX     12 byte
} ExportVertex;

typedef struct VertexTransformCommand
{
    const void*         Data;
    const uint32_t*     Indices;
    const InputElementDescriptor* Elements;
    int                 NumInputElements;
    mat4                ProjectionMatrix;
} VertexTransformCommand;

#if PJD_DEBUG_VIEW_ENABLED
typedef struct DebugParams
{
    float dudx;
    float dudy;
    float dvdx;
    float dvdy;
};
#endif

#if PJD_DEBUG_VIEW_ENABLED
#define DEBUG_VIEW_ONLY_ARG(...) ,__VA_ARGS__
#else
#define DEBUG_VIEW_ONLY_ARG(...)
#endif

extern struct ThreadPool*    ThreadPool;
extern RasterContext         Ctx;
extern struct ExportBuffer*  ExportBuffer;
extern ScreenTile            Tiles[MAX_TILES];
extern vec2i                 TileIndexToCoord[TILE_WIDTH * TILE_HEIGHT];
extern int                   CoordToTileIndex[TILE_WIDTH][TILE_HEIGHT];

static PJD_INLINE int Edge(int x0, int y0, int x1, int y1, int x, int y)
{
    return (y - y0) * (x1 - x0) - (x - x0) * (y1 - y0);
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

void WriteFramebufferDirect(int offset, rgba8 color);

rgba8 SampleTextureLod(int sx, int sy, float u, float v, float mipLevel);

void RunVertexTransform(bool parallelize, int numPrimitives, VertexTransformCommand* command);
void RunTriangleBinning(bool parallelize, const struct Range* range);
void RunRasterizeTriangles(bool parallelize, const struct Range* range);

#if DEBUG_TILE_CLASSIFICATION
void DebugViewTileCoverage();
#endif // DEBUG_TILE_CLASSIFICATION

static rgba8 ShadePixelDefault(float mipLevel, const Interpolants* interp)
{
    return SampleTextureLod(interp->px, interp->py, interp->u, interp->v, mipLevel);
}

#endif // PJD_RASTER_INTERNAL_H