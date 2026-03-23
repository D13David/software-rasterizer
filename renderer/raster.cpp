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

static BOOL SetLargePagePrivilege()
{
    BOOL ret = TRUE;
    HANDLE hToken = INVALID_HANDLE_VALUE;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        goto fail;
    }

    TOKEN_PRIVILEGES tp;
    LUID luid;

    if (!LookupPrivilegeValue(NULL, L"SeLockMemoryPrivilege", &luid)) {
        goto fail;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) {
        goto fail;
    }

    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        goto fail;
    }

    goto cleanup;

fail:
    ret = FALSE;
cleanup:
    if (hToken != INVALID_HANDLE_VALUE) CloseHandle(hToken);

    return ret;
}

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

    Ctx.DepthWriteEnabled = true;

    Ctx.PixelShader = &ShadePixelDefault;

    SYSTEM_INFO systemInfo;
    GetSystemInfo(&systemInfo);
#if 0
    int numThreads = 1;
#else
    int numThreads = systemInfo.dwNumberOfProcessors;
#endif
    ThreadPool = ThreadPoolInit(numThreads, 1024 * 32);

    size_t regionSize = THREAD_GROUP_SIZE_VTRANSFORM * sizeof(ExportVertex) * 15; // upper bound for clipped vertices per region
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

    SetLargePagePrivilege();

    SIZE_T largePageMinimum = GetLargePageMinimum();
    int size = Align(MAX_TILES * MAX_TRIS_PER_TILE * sizeof(int), largePageMinimum);

    // FIXME: use a scratchpad allocator to allocate number of binned triangles per tile buffers intead of 
    //        using a maximum length for each
    int* tileBinningBuffer = (int*)VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
    for (int i = 0; i < MAX_TILES; ++i) {
        TileBins[i].BinnedTriangles = &tileBinningBuffer[i * MAX_TRIS_PER_TILE];
        TileBins[i].NumTriangles = 0;
    }

    tileBinningBuffer = (int*)VirtualAlloc(NULL, MAX_MACRO_TILES * MAX_TRIS_PER_TILE * sizeof(int), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    for (int i = 0; i < MAX_MACRO_TILES; ++i) {
        MacroTileBins[i].BinnedTriangles = &tileBinningBuffer[i * MAX_TRIS_PER_TILE];
        MacroTileBins[i].NumTriangles = 0;
    }
}

void RasterizerDestroy()
{
    VirtualFree(TileBins[0].BinnedTriangles, 0, MEM_RELEASE);
    VirtualFree(MacroTileBins[0].BinnedTriangles, 0, MEM_RELEASE);
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

void SetDepthWrite(bool enabled)
{
    Ctx.DepthWriteEnabled = enabled;
}

#if PJD_DEBUG_VIEW_ENABLED
void SetDebugMode(DebugMode mode)
{
    Ctx.DebugMode = mode;
}
#endif

void SetPixelShader(PS shader)
{
    Ctx.PixelShader = shader != NULL ? shader : ShadePixelDefault;
}

void GetViewport(uint32_t* width, uint32_t* height)
{
    assert(width && height);
    *width = Ctx.Out.Width;
    *height = Ctx.Out.Height;
}

void Clear(rgba8 color)
{
    PROFILE_AUTO("Frame Buffer Clear");

    int bufferSize = PJD_FB_WIDTH * PJD_FB_HEIGHT;

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
    assert(offset < PJD_FB_WIDTH * PJD_FB_HEIGHT);
    ((rgba8*)ColorBuffer[Frame])[offset] = color;
}

void DrawPixel(int x, int y, rgba8 color)
{
    assert(x < PJD_FB_WIDTH && y < PJD_FB_HEIGHT);
#if ENABLE_TILED_FRAMEBUFFER_LAYOUT
    int tx = x / TILE_WIDTH;
    int ty = y / TILE_HEIGHT;
    int offset = ((ty * TILE_COUNT_X + tx) * (TILE_WIDTH * TILE_HEIGHT)) + CoordToTileIndex[y-ty*TILE_WIDTH][x-tx*TILE_HEIGHT];
#else
    int offset = y * PJD_FB_WIDTH + x;
#endif
    WriteFramebufferDirect(offset, color);
}

void DrawPixelToScreen(int x, int y, rgba8 color)
{
    assert(x < PJD_FB_WIDTH && y < PJD_FB_HEIGHT);
    ((rgba8*)Ctx.Out.CB)[y*PJD_FB_WIDTH+x] = color;
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

    uint32_t x = (int)(u * mipWidth);
    uint32_t y = (int)(v * mipHeight);

    rgba8* mipData = (rgba8*)(((uint8_t*)texture.Data) + texture.MipOffsets[mip]);
    return mipData[y * mipWidth + x];
}

static void RunDrawTrianglesWireframe(const Range* range, rgba8 color)
{
    int numVertices = (range->Size / sizeof(ExportVertex));
    ExportVertex* vertexStart = (ExportVertex*)range->Ptr;
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

    while (true)
    {
        Range range = ExportBufferReadPublished(ExportBuffer);
        if (range.Ptr == NULL) {
            break;
        }

        
        RunTriangleBinning(parallel, &range);
        RunRasterizeTriangles(parallel, &range);

        if (Ctx.DrawMode == Wireframe)
        {
            RasterMode2D(false);
            RunDrawTrianglesWireframe(&range, COLOR(0.1, 0.1, 0.1));
            RasterMode2D(true);
        }
    }

    ExportBufferReset(ExportBuffer, false);
}

static void ResolveTiledFrameBuffer(size_t id, int startTile, int endTile, void* context)
{
    const int PixelsPerTile = TILE_WIDTH * TILE_HEIGHT;

    rgba8* outCB = (rgba8*)Ctx.Out.CB;
    const uint32_t* TileBuffer = ColorBuffer[Frame] + startTile * PixelsPerTile;
    const uint32_t* TileBufferEnd = &ColorBuffer[Frame][PJD_FB_WIDTH*PJD_FB_HEIGHT];

    for (int tileIndex = startTile; tileIndex < endTile; ++tileIndex)
    {
        int tileOriginX = (tileIndex % TILE_COUNT_X) * TILE_WIDTH;
        int tileOriginY = (tileIndex / TILE_COUNT_X) * TILE_HEIGHT;

        uint32_t scratch[TILE_WIDTH * TILE_HEIGHT];

        for (int pos = 0; pos < PixelsPerTile; ++pos)
        {
            int x, y;
            Decode6BitMorton(pos, &x, &y);
            scratch[y * TILE_WIDTH + x] = ((TileBuffer + pos) < TileBufferEnd) ? TileBuffer[pos] : 0;
        }

        int tileHeight = min(tileOriginY + TILE_HEIGHT, PJD_FB_HEIGHT - 1) - tileOriginY;
        int tileWidth = min(tileOriginX + TILE_WIDTH, PJD_FB_WIDTH - 1) - tileOriginX;
        for (int y = 0; y < tileHeight; ++y)
        {
            memcpy(
                &outCB[(tileOriginY + y) * PJD_FB_WIDTH + tileOriginX],
                &scratch[y * TILE_WIDTH],
                tileWidth * sizeof(uint32_t));
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
        for (int i = 0; i < PJD_FB_HEIGHT * PJD_FB_WIDTH; ++i) 
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
    ParallelFor(ThreadPool, 0, TILE_COUNT_X*TILE_COUNT_Y, 8, ResolveTiledFrameBuffer, true, NULL);
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
    memcpy(outCB, ColorBuffer[Frame], PJD_FB_WIDTH * PJD_FB_HEIGHT * sizeof(uint32_t));
#endif

    if (ShowPerformanceMetrics) {
        DebugDrawExportBufferBuckets(ExportBuffer, 0, 300, PJD_FB_WIDTH / 4, 50);
    }

    ExportBufferReset(ExportBuffer, true);
}

void SaveScreenshot(const char* filename)
{
    WriteToTgaFile(filename, PJD_FB_WIDTH, PJD_FB_HEIGHT, (uint8_t*)Ctx.Out.CB);
}