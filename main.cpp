#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <stdlib.h>

#include "thirteen.h"

#include "mathlib.h"
#include "mesh_loader.h"
#include "texture_loader.h"
#include "raster.h"
#include "fps_meter.h"
#include "uarch_loader.h"
#include "profile.h"
#include "render_stats.h"
#include "font_render.h"

#define FPS_METER_WIDTH 200
#define FPS_METER_HEIGHT 70
#define FB_WIDTH 1920
#define FB_HEIGHT 1080

typedef struct Command
{
    int                  KeyCode;
    const char*          Help;
    void(*CommandFunc)();
} Command;

bool WireFrameOverlay = false;
bool ShowHelp = false;
bool ShowPerformanceMetrics = true;

static Command Commands[] =
{
    { VK_F1, "Toggle Help",              []() { ShowHelp = !ShowHelp; }},
    { 'W',   "Toggle Wireframe Overlay", []() { WireFrameOverlay = !WireFrameOverlay; } },
    { 'H',   "Toggle Perf Metrics",      []() { ShowPerformanceMetrics = !ShowPerformanceMetrics; } },
#if DEBUG_VIEW
    { '0',   "Scene Rendering",          []() { SetDebugMode(DM_None); }},
    { '1',   "Show Face Mip-Levels",     []() { SetDebugMode(DM_FaceMipMapLevel); } },
    { '2',   "Show Face Derivatives",    []() { SetDebugMode(DM_FaceDerivatives); } },
    { '3',   "Show Tile Classification", []() { SetDebugMode(DM_TileClassification); } },
#endif
    { 0, 0 }
};

static DWORD LastTime;
static float DeltaTime;

Mesh* Gmesh[10];
int numMeshes;

static void DrawMesh(Mesh* mesh, float tx, float ty, float tz)
{
    PROFILE_AUTO("DrawMesh");

    // object transform
    mat4 ObjectTransform;
    CreateMatrixTransform(tx, ty, tz, ObjectTransform);

    static float angle = 180;
    angle += 10 * DeltaTime;
    if (angle > 360.0f) angle -= 360.0f;

    /*mat4_t ObjectRotateX;
    CreateMatrixRotateX(DEG2RAD(-20.0f), ObjectRotateX);*/

    mat4 ObjectRotateY;
    CreateMatrixRotateY(DEG2RAD(angle), ObjectRotateY);

    mat4 WorldMat;
    //MatrixMultiply(ObjectRotateX, ObjectRotateY, WorldMat);
    Matrix4Mul(ObjectRotateY, ObjectTransform, WorldMat);

    //// projection
    mat4 ProjectionMat;
    CreateMatrixPerspectiveFovLH(60.0f, FB_WIDTH / (float)FB_HEIGHT, 0.1f, 2000.0f, ProjectionMat);
    
    // world view projection
    mat4 WorldViewProj;
    Matrix4Mul(WorldMat, ProjectionMat, WorldViewProj);

    for (int i = 0; i < mesh->NumSurfaces; ++i)
    {
        const Surface* surface = &mesh->Surfaces[i];

        SetTextureView(surface->Texture);

        DrawTriangleList
        (
            mesh->NumAnimSeqs == 0 ? mesh->VertexBuffer : mesh->FrameCache,
            surface->IndexBuffer,
            mesh->InputDesc,
            mesh->NumInputElements,
            surface->NumPrimitives, 
            WorldViewProj, 
            true
        );

        if (WireFrameOverlay)
        {
            SetDrawMode(DrawMode::Wireframe);

            DrawTriangleList
            (
                mesh->NumAnimSeqs == 0 ? mesh->VertexBuffer : mesh->FrameCache,
                surface->IndexBuffer,
                mesh->InputDesc,
                mesh->NumInputElements,
                surface->NumPrimitives,
                WorldViewProj,
                true
            );

            SetDrawMode(DrawMode::Solid);
        }
    }
}

static void DrawFrame()
{
    PROFILE_AUTO("Frame");

    {
        PROFILE_AUTO("Update Animations");

        static float frame = 0;
        frame += DeltaTime * 10;

        for (int i = 0; i < numMeshes; ++i)
        {
            auto anim = FindAnimSequence(Gmesh[i], "Walk");
            if (!anim) {
                anim = FindAnimSequence(Gmesh[i], "Float");
            }
            UpdateFrame(Gmesh[i], anim, frame);
        }
    }

    Clear(RGB(0, 0, 0));

    SetTextureFilter(TextureFilter::Unreal);
    
    SetDrawMode(DrawMode::Solid);
    for (int i = 0; i < numMeshes; ++i) {
        DrawMesh(Gmesh[i], (i - numMeshes/2)*250, 0, 400);
    }

    ResolveFrameBuffer();

    if (ShowPerformanceMetrics)
    {
        FPSMeterUpdate();
        FPSMeterDraw(0, FB_HEIGHT - 1 - FPS_METER_HEIGHT, FPS_METER_WIDTH, FPS_METER_HEIGHT);
        DrawRenderStats(FB_WIDTH - 170, 10);
    }

    if (ShowHelp)
    {
        const int width  = 300;
        const int height = 200;
        int top = (FB_HEIGHT - height) / 2;
        int left = (FB_WIDTH - width) / 2;
        DrawRectangle(left, top, width, height, COLOR(0.4, 0.4, 0.4));

        int offset = top + 5;
        for (int i = 0; Commands[i].Help; ++i)
        {
            char keyCodeName[128];
            UINT scanCode = MapVirtualKey(Commands[i].KeyCode, MAPVK_VK_TO_VSC);
            LONG result = GetKeyNameTextA(scanCode << 16, keyCodeName, sizeof(keyCodeName));
            FntWriteString(Format("%-5s - %s", keyCodeName, Commands[i].Help), left + 5, offset), offset += 10;
        }
    }
}

static void DrawScene2D()
{
    Clear(0);

    PROFILE_AUTO("Frame");
    DrawEllipseFilled(FB_WIDTH/2, FB_HEIGHT/2, 100, 100, COLOR(1, 1, 1), 6);
}

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

    float* depthBuffer = (float*)_aligned_malloc(FB_WIDTH * FB_HEIGHT * sizeof(float), 32);

    RasterizerInitialize({
            .BufferDesc = {
                .Width = FB_WIDTH,
                .Height = FB_HEIGHT
            },
            .FrameBufferPtr = frameBuffer,
            .DepthBufferPtr = depthBuffer

        });

#if 1
    auto archive = OpenArchive("../Unreal/System/UnrealI.u");
    if (archive)
    {
        //DumpExportTable(archive, "Mesh");
        struct LoadInfo
        {
            const char* name;
            const char* skin;
        };
        LoadInfo loadInfos[] = {
            { "Skaarjw", "./meshes/textures/Skaarjw3.tga" },
            { "Merc", "./meshes/textures/JMerc1.tga" },
            { "Male2", "./meshes/textures/Ash.tga" },
            { "Female2", "./meshes/textures/F2Female2.tga" },
            { "Female1", "./meshes/textures/gina.tga" },
            { "Brute1", "./meshes/textures/jBrute1.tga" },
            { "GasBagM", "./meshes/textures/GasBag2.tga" },
            { NULL, NULL }
        };
        for (numMeshes = 0; loadInfos[numMeshes].name; ++numMeshes) {
            if (LoadMeshFromArchive(archive, loadInfos[numMeshes].name, &Gmesh[numMeshes])) {
                Gmesh[numMeshes]->Surfaces[0].Texture = LoadTexture(loadInfos[numMeshes].skin);
            }
        }
        CloseArchive(archive);
    }

    if (!Gmesh)
    {
        MessageBox(NULL, L"Failed to load mesh", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }
#else
    //if (!LoadMeshFromFile("./meshes/Base mesh.obj", &Gmesh))
    if (!GenerateMeshQuad(&Gmesh[numMeshes++], 220, 500))
    {
        MessageBox(NULL, L"Failed to load mesh", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }
#endif

    FPSMeterInitialize();

    LastTime = GetTickCount();

    while (true)
    {
        DWORD currentTime = GetTickCount();
        DeltaTime = (currentTime - LastTime) / 1000.0f;
        LastTime = currentTime;

        HandleUserInput();

        ProfilerReset();
        ResetRenderStats();
        DrawFrame();

        if (ShowPerformanceMetrics) {
            DrawProfilerStats(10, 10, 300);
        }

        if (!Thirteen::Render()) {
            break;
        }
    }
exit:
    if (depthBuffer) {
        _aligned_free(depthBuffer);
    }
    for (int i = 0; i < numMeshes; ++i) {
        MeshFree(Gmesh[i]);
    }
    Thirteen::Shutdown();

    return 0;
}
