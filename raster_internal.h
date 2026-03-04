#ifndef PJD_RASTER_INTERNAL_H
#define PJD_RASTER_INTERNAL_H

#include <atomic>

using AtomicInt = std::atomic<int>;

// enable checkerboard rendering (prototype)
#define ENABLE_CHECKERBOARD_RENDERING 0

// enable tiled framebuffer layout
#define ENABLE_TILED_FRAMEBUFFER_LAYOUT 1

#if DEBUG_VIEW
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
#define MAX_TRIS_PER_TILE   1024
#define MAX_TILES           ((1920 / TILE_WIDTH) * (1080 / TILE_HEIGHT))

#define THREAD_GROUP_SIZE  128

typedef struct RasterContext
{
    struct {
        int         Width;
        int         Height;
        void* CB;
        void* DB;
    } Out;
    TextureFilter   Filter;
    TextureView     Texture;
    DrawMode        DrawMode;
#if DEBUG_VIEW
    DebugMode       DebugMode;
#endif
} RasterContext;

typedef struct ScreenTile
{
    int BinnedTriangles[MAX_TRIS_PER_TILE];
    AtomicInt NumTriangles;
} ScreenTile;

typedef struct ExportVertex
{
    int   ScreenX;
    int   ScreenY;
    float InvW;             // 1 / w
    float ZOverW;           // z / w
    float UOverW;           // u / w
    float VOverW;           // v / w
} ExportVertex;

typedef struct VertexTransformCommand
{
    const void*         Data;
    const uint16_t*     Indices;
    const InputElement* Elements;
    int                 NumInputElements;
    mat4                ProjectionMatrix;
} VertexTransformCommand;

extern struct ThreadPool*   ThreadPool;
extern RasterContext        Ctx;
extern struct ExportBuffer* ExportBuffer;
extern ScreenTile           Tiles[MAX_TILES];
extern vec2i                TileIndexToCoord[TILE_WIDTH * TILE_HEIGHT];
extern int                  CoordToTileIndex[TILE_WIDTH][TILE_HEIGHT];

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

void WriteFramebufferDirect(int offset, Color color);

Color SampleTextureLod(int sx, int sy, float u, float v, float mipLevel);

void RunVertexTransform(bool parallelize, int numPrimitives, VertexTransformCommand* command);
void RunTriangleBinning(bool parallelize);
void RunRasterizeTriangles(bool parallelize);

#if DEBUG_TILE_CLASSIFICATION
void DebugViewTileCoverage();
#endif // DEBUG_TILE_CLASSIFICATION

#endif // PJD_RASTER_INTERNAL_H