#include "raster.h"
#include "raster_internal.h"
#include "texture_loader.h"
#include "common/thread_pool.h"
#include "common/export_buffer.h"
#include "common/profile.h"
#include "common/parallel_for.h"

#define EXPORT_BUFFER_SIZE (128 * 1024 * 1024)

RasterContext       Ctx;
ThreadPoolHandle    ThreadPool;
vec2i               TileIndexToCoord[TILE_WIDTH * TILE_HEIGHT];
int                 CoordToTileIndex[TILE_WIDTH][TILE_HEIGHT];

static uint32_t*    ColorBuffer[2];
static int          Frame;

void RasterizerInitialize(const RasterizerDesc& init)
{
    assert(init.FrameBufferPtr != NULL);

    Ctx.Out = {
        .Width = (int)init.BufferDesc.Width,
        .Height = (int)init.BufferDesc.Height,
        .CB = init.FrameBufferPtr,
        .DB = init.DepthBufferPtr
    };

#if PJD_DEBUG_VIEW_ENABLED
    Ctx.DebugMode = DM_None;
#endif

    Ctx.PixelShader = &ShadePixelDefault;

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

    Frame = 0;
    for (int i = 0; i < 2; ++i) {
        ColorBuffer[i] = (uint32_t*)malloc(Ctx.Out.Width * Ctx.Out.Height * sizeof(uint32_t));
    }

    for (int i = 0; i < TILE_WIDTH * TILE_HEIGHT; ++i) {
        Decode6BitMorton(i, &TileIndexToCoord[i][0], &TileIndexToCoord[i][1]);
    }
    for (int y = 0; y < TILE_HEIGHT; ++y)
    {
        for (int x = 0; x < TILE_WIDTH; ++x) {
            CoordToTileIndex[y][x] = Encode6BitMorton(x, y);
        }
    }

    int* tileBinningBuffer = (int*)malloc(MAX_TILES * MAX_TRIS_PER_TILE * sizeof(int));
    for (int i = 0; i < MAX_TILES; ++i) {
        Tiles[i].BinnedTriangles = &tileBinningBuffer[i * MAX_TRIS_PER_TILE];
    }   
}

void RasterizerDestroy()
{
    free(Tiles[0].BinnedTriangles);
    ThreadPoolDestroy(ThreadPool, ShutdownMode::IMMEDIATE);
}

void SetTextureFilter(TextureFilter filter)
{
    Ctx.Filter = filter;
}

void SetTextureView(TextureView texture)
{
    Ctx.Texture = texture;
}

void SetDrawMode(DrawMode drawMode)
{
    Ctx.DrawMode = drawMode;
}

#if PJD_DEBUG_VIEW_ENABLED
void SetDebugMode(DebugMode mode)
{
    Ctx.DebugMode = mode;
}
#endif

void SetPixelShader(PS shader)
{
    Ctx.PixelShader = shader;
}

void Clear(rgba8 color)
{
    PROFILE_AUTO("Frame Buffer Clear");

    int bufferSize = FB_WIDTH * FB_HEIGHT;

    rgba8* colorBuffer = (rgba8*)ColorBuffer[Frame];

    for (int i = 0; i < bufferSize; ++i) {
        colorBuffer[i] = color;
    } 

    float* depthBuffer = (float*)Ctx.Out.DB;
    for (int i = 0; i < bufferSize; ++i) {
        depthBuffer[i] = 1.0f;
    }
}

void WriteFramebufferDirect(int offset, rgba8 color)
{
    assert(offset < FB_WIDTH * FB_HEIGHT);
    ((rgba8*)ColorBuffer[Frame])[offset] = color;
}

void DrawPixel(int x, int y, rgba8 color)
{
    assert(x < FB_WIDTH && y < FB_HEIGHT);
#if ENABLE_TILED_FRAMEBUFFER_LAYOUT
    int tx = x / TILE_WIDTH;
    int ty = y / TILE_HEIGHT;
    int offset = ((ty * TILE_COUNT_X + tx) * (TILE_WIDTH * TILE_HEIGHT)) + CoordToTileIndex[y-ty*TILE_WIDTH][x-tx*TILE_HEIGHT];
#else
    int offset = y * FB_WIDTH + x;
#endif
    WriteFramebufferDirect(offset, color);
}

void DrawPixelToScreen(int x, int y, rgba8 color)
{
    assert(x < FB_WIDTH && y < FB_HEIGHT);
    ((rgba8*)Ctx.Out.CB)[y*FB_WIDTH+x] = color;
}

rgba8 SampleTextureLod(int sx, int sy, float u, float v, float mipLevel)
{
    TextureView texture = Ctx.Texture;

    assert(texture.Data != NULL);

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

    rgba8* mipData = (rgba8*)(((uint8_t*)texture.Data) + texture.MipOffsets[mip]);
    return mipData[y * mipWidth + x];
}

static void RunDrawTrianglesWireframe(rgba8 color)
{
    size_t numVertices = (ExportBufferUsed(ExportBuffer) / sizeof(ExportVertex));
    ExportVertex* vertexStart = (ExportVertex*)ExportBufferData(ExportBuffer);
    for (int i = 0; i < numVertices; i += 3)
    {
        ExportVertex* v0 = &vertexStart[i+0];
        ExportVertex* v1 = &vertexStart[i+1];
        ExportVertex* v2 = &vertexStart[i+2];
        DrawLine(v0->ScreenX, v0->ScreenY, v1->ScreenX, v1->ScreenY, color, 2);
        DrawLine(v1->ScreenX, v1->ScreenY, v2->ScreenX, v2->ScreenY, color, 2);
        DrawLine(v2->ScreenX, v2->ScreenY, v0->ScreenX, v0->ScreenY, color, 2);
    }
}

void DrawTriangleList(const void* data, const uint32_t* indices, const InputElementDescriptor* elements, int numInputElements, int numPrimitives, mat4 ProjectionMatrix, bool parallel)
{
    VertexTransformCommand command = {
        .Data = data,
        .Indices = indices,
        .Elements = elements,
        .NumInputElements = numInputElements
    };

    CopyMatrix(ProjectionMatrix, command.ProjectionMatrix);

    RunVertexTransform(parallel, numPrimitives, &command);

    if (Ctx.DrawMode == Solid)
    {
        RunTriangleBinning(parallel);
        RunRasterizeTriangles(parallel);
    }
    else
    {
        RunDrawTrianglesWireframe(COLOR(0.75, 0.75, 0.75));
    }

    // TODO: we probably don't need to reset the buffer here
    ExportBufferReset(ExportBuffer);
}

static void ResolveTiledFrameBuffer(size_t id, int startTile, int endTile, void* context)
{
    const int PixelsPerTile = TILE_WIDTH * TILE_HEIGHT;

    rgba8* outCB = (rgba8*)Ctx.Out.CB;
    const uint32_t* TileBuffer = ColorBuffer[Frame] + startTile * PixelsPerTile;

    for (int tileIndex = startTile; tileIndex < endTile; ++tileIndex)
    {
        int tileOriginX = (tileIndex % TILE_COUNT_X) * TILE_WIDTH;
        int tileOriginY = (tileIndex / TILE_COUNT_X) * TILE_HEIGHT;

        for (int pos = 0; pos < PixelsPerTile; ++pos)
        {
            int px = tileOriginX + TileIndexToCoord[pos][0];
            int py = tileOriginY + TileIndexToCoord[pos][1];
            outCB[py * FB_WIDTH + px] = TileBuffer[pos];
        }

        TileBuffer += PixelsPerTile;
    }
}

void ResolveFrameBuffer()
{
    PROFILE_AUTO("Resolve");

#if DEBUG_TILE_CLASSIFICATION
    if (Ctx.DebugMode == DM_TileClassification) {
        DebugViewTileCoverage();
    }
#endif 

#if PJD_DEBUG_VIEW_ENABLED
    if (Ctx.DebugMode == DM_DepthBuffer)
    {
        // just overrid the colorbuffer so tile resolve is done automatically
        for (int i = 0; i < FB_HEIGHT * FB_WIDTH; ++i) 
        {
            const float zf = 1000.0f;
            const float zn = 10.0f;
            float z = (zn * zf) / (zf - ((float*)Ctx.Out.DB)[i] * (zf - zn));
            ColorBuffer[Frame][i] = COLOR(Saturate(z / zf), 0, 0);
        }
    }
#endif

    rgba8* outCB = (rgba8*)Ctx.Out.CB;
#if ENABLE_TILED_FRAMEBUFFER_LAYOUT
    ParallelFor(ThreadPool, 0, TILE_COUNT_X*TILE_COUNT_Y, 8, ResolveTiledFrameBuffer, NULL);
#elif ENABLE_CHECKERBOARD_RENDERING
    Frame = 1 - Frame;
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
#else
    memcpy(outCB, ColorBuffer[Frame], FB_WIDTH * FB_HEIGHT * sizeof(uint32_t));
#endif
}

void SaveScreenshot(const char* filename)
{
    WriteToTgaFile(filename, FB_WIDTH, FB_HEIGHT, (uint8_t*)Ctx.Out.CB);
}