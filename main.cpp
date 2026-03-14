#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shlobj.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <stdlib.h>

#define THIRTEEN_IMPLEMENTATION
#include "external/thirteen.h"

#include "renderer/fps_meter.h"
#include "common/profile.h"
#include "renderer/render_stats.h"
#include "shared.h"
#include "renderer/raster.h"
#include "renderer/texture_loader.h"

#define FPS_METER_WIDTH 200
#define FPS_METER_HEIGHT 70

extern bool SceneInitialize();
extern void SceneDestroy();
extern void SceneRenderFrame();
extern void SceneRenderOverlay2D();

bool WireFrameOverlay = false;
float DeltaTime;

static bool ShowHelp = false;
static bool ShowPerformanceMetrics = true;
static ULONGLONG LastTime;
static char ApplicationDataPath[MAX_PATH];

static void CaptureScreenshot();

typedef struct Command
{
    int                  KeyCode;
    const char* Help;
    void(*CommandFunc)();
} Command;

static Command Commands[] =
{
    { VK_F1, "Toggle Help",              []() { ShowHelp = !ShowHelp; }},
    { 'W',   "Toggle Wireframe Overlay", []() { WireFrameOverlay = !WireFrameOverlay; } },
    { 'H',   "Toggle Perf Metrics",      []() { ShowPerformanceMetrics = !ShowPerformanceMetrics; } },
#if PJD_DEBUG_VIEW_ENABLED
    { '0',   "Scene Rendering",          []() { SetDebugMode(DM_None); }},
    { '1',   "Show Face Mip-Levels",     []() { SetDebugMode(DM_FaceMipMapLevel); } },
    { '2',   "Show Face Derivatives",    []() { SetDebugMode(DM_FaceDerivatives); } },
    { '3',   "Show Tile Classification", []() { SetDebugMode(DM_TileClassification); } },
    { '4',   "Show Depth",               []() { SetDebugMode(DM_DepthBuffer); } },
#endif
    { VK_F11,"Save Screenshot",          []() { CaptureScreenshot(); } },
    { 0, 0 }
};

static void HandleUserInput()
{
    for (int i = 0; Commands[i].Help; ++i)
    {
        if (Thirteen::GetKey(Commands[i].KeyCode) && !Thirteen::GetKeyLastFrame(Commands[i].KeyCode)) {
            Commands[i].CommandFunc();
        }
    }
}

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
    uint8_t* frameBuffer = Thirteen::Init(FB_WIDTH, FB_HEIGHT, false);
    Thirteen::SetVSync(false);

    ProfilerInitialize();

    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, ApplicationDataPath)))
    {
        strcat_s(ApplicationDataPath, "/SoftwareRasterizer");
        if (!CreateDirectoryA(ApplicationDataPath, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            return 0;
        }
    }

    float* depthBuffer = (float*)_aligned_malloc(FB_WIDTH * FB_HEIGHT * sizeof(float), 32);

    RasterizerInitialize({
            .BufferDesc = {
                .Width = FB_WIDTH,
                .Height = FB_HEIGHT
            },
            .FrameBufferPtr = frameBuffer,
            .DepthBufferPtr = depthBuffer

        });


    if (!SceneInitialize()) {
        MessageBox(NULL, L"Failed to load mesh", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    FPSMeterInitialize();

    LastTime = GetTickCount64();

    while (true)
    {
        ULONGLONG currentTime = GetTickCount64();
        DeltaTime = (currentTime - LastTime) / 1000.0f;
        LastTime = currentTime;

        HandleUserInput();

        ProfilerReset();
#if PJD_USE_RENDER_STATS
        ResetRenderStats();
#endif // PJD_USE_RENDER_STATS

        {
            PROFILE_AUTO("Scene Frame Render");
            SceneRenderFrame();
        }
        
        ResolveFrameBuffer();

        // draw directly to screen after resolve
        RasterMode2D(true);

        {
            PROFILE_AUTO("Scene Overlay 2D");
            SceneRenderOverlay2D();
        }

        if (ShowPerformanceMetrics)
        {
            FPSMeterUpdate();
            FPSMeterDraw(0, FB_HEIGHT - 100 - FPS_METER_HEIGHT, FPS_METER_WIDTH, FPS_METER_HEIGHT);
#if PJD_USE_RENDER_STATS
            DrawRenderStats(FB_WIDTH - 200, 10);
#endif // PJD_USE_RENDER_STATS
        }

        if (ShowHelp)
        {
            const int width = 300;
            const int height = 200;
            int top = (FB_HEIGHT - height) / 2;
            int left = (FB_WIDTH - width) / 2;
            DrawRectangle(left, top, width, height, COLOR(0.4f, 0.4f, 0.4f), CLOSE_DOT_FILL);

            int offset = top + 5;
            for (int i = 0; Commands[i].Help; ++i)
            {
                char keyCodeName[128];
                UINT scanCode = MapVirtualKey(Commands[i].KeyCode, MAPVK_VK_TO_VSC);
                LONG result = GetKeyNameTextA(scanCode << 16, keyCodeName, sizeof(keyCodeName));
                WriteString(Format("%-5s - %s", keyCodeName, Commands[i].Help), left + 5, offset), offset += 10;
            }
        }

        if (ShowPerformanceMetrics) {
            DrawProfilerStats(10, 10, 300);
        }

        if (!Thirteen::Render()) {
            break;
        }
    }

    SceneDestroy();

    if (depthBuffer) {
        _aligned_free(depthBuffer);
    }
    Thirteen::Shutdown();

    return 0;
}

static void CaptureScreenshot()
{
    char filename[MAX_PATH];

    for (int i = 0; ; ++i)
    {
        sprintf_s(filename, "%s/Screenshot_%d.tga", ApplicationDataPath, i);
        DWORD attribs = GetFileAttributesA(filename);
        if (attribs == INVALID_FILE_ATTRIBUTES) {
            break;
        }
    }

    SaveScreenshot(filename);
}