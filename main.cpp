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

#define FPS_METER_WIDTH 200
#define FPS_METER_HEIGHT 70
#define FB_WIDTH 1280
#define FB_HEIGHT 720

Mesh* Gmesh[10];
int numMeshes;
DWORD LastTime;
float DeltaTime;

static void DrawMesh(Mesh* mesh, float tx, float ty, float tz)
{
#if 0
    // object transform
    mat4 ObjectTransform;
    CreateMatrixTransform(tx, ty, tz, ObjectTransform);

    static float angle = 0;
    angle += 10 * DeltaTime;
    if (angle > 360.0f) angle -= 360.0f;

    /*mat4_t ObjectRotateX;
    CreateMatrixRotateX(DEG2RAD(-20.0f), ObjectRotateX);*/

    mat4 ObjectRotateY;
    CreateMatrixRotateY(DEG2RAD(angle), ObjectRotateY);

    mat4 WorldMat;
    //MatrixMultiply(ObjectRotateX, ObjectRotateY, WorldMat);
    Matrix4Mul(ObjectRotateY, ObjectTransform, WorldMat);
#else
    static float angle = 70.0;
    static bool forward = true;
    if (forward)
    {
        angle += 10 * DeltaTime;
        if (angle > 85) {
            forward = false;
        }
    }
    else
    {
        angle -= 10 * DeltaTime;
        if (angle < 70) {
            forward = true;
        }
    }

    mat4 ObjectTransform;
    CreateMatrixTransform(tx, ty, tz, ObjectTransform);

    mat4 ObjectRotateX;
    CreateMatrixRotateX(DEG2RAD(angle), ObjectRotateX);

    mat4 WorldMat;
    Matrix4Mul(ObjectRotateX, ObjectTransform, WorldMat);
#endif

    //// projection
    mat4 ProjectionMat;
    CreateMatrixPerspectiveFovLH(60.0f, FB_WIDTH / (float)FB_HEIGHT, 0.1f, 2000.0f, ProjectionMat);
    
    // world view projection
    mat4 WorldViewProj;
    Matrix4Mul(WorldMat, ProjectionMat, WorldViewProj);

    for (int i = 0; i < mesh->NumSurfaces; ++i)
    {
        const Surface* surface = &mesh->Surfaces[i];

        srSetTextureView(surface->Texture);

        srDrawTriangleList
        (
            mesh->NumAnimSeqs == 0 ? mesh->VertexBuffer : mesh->FrameCache,
            surface->IndexBuffer,
            mesh->InputDesc,
            mesh->NumInputElements,
            surface->NumPrimitives, 
            WorldViewProj, 
            true
        );
    }
}

static void DrawFrame()
{
#if 0
    static float frame = 0;
    frame += DeltaTime * 10;

    for (int i = 0; i < numMeshes; ++i) 
    {
        auto anim = FindAnimSequence(Gmesh[i], "Walk");
        if (!anim) {
            anim = FindAnimSequence(Gmesh[i], "Float");
        }
        UpdateGetFrame(Gmesh[i], anim, frame);
    }
#endif

    srClear(RGB(0, 0, 0));

    //srSetTextureFilter(TextureFilter::Unreal);

    for (int i = 0; i < numMeshes; ++i) {
        DrawMesh(Gmesh[i], (i - numMeshes/2)*250, 0, 260);
    }

    FPSMeterUpdate();
    FPSMeterDraw(0, FB_HEIGHT - 1 - FPS_METER_HEIGHT, FPS_METER_WIDTH, FPS_METER_HEIGHT);
}

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
    uint8_t* frameBuffer = Thirteen::Init(FB_WIDTH, FB_HEIGHT);
    Thirteen::SetVSync(false);

    float* depthBuffer = (float*)malloc(FB_WIDTH * FB_HEIGHT * sizeof(float));

    srInitialize({
            .BufferDesc = {
                .Width = FB_WIDTH,
                .Height = FB_HEIGHT
            },
            .FrameBufferPtr = frameBuffer,
            .DepthBufferPtr = depthBuffer

        });

#if 0
    auto archive = OpenArchive("../Unreal/System/UnrealI.u");
    if (archive)
    {
        DumpExportTable(archive, "Mesh");
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

        DrawFrame();

        if (!Thirteen::Render()) {
            break;
        }
    }
exit:
    if (depthBuffer) {
        free(depthBuffer);
    }
    for (int i = 0; i < numMeshes; ++i) {
        MeshFree(Gmesh[i]);
    }
    Thirteen::Shutdown();

    return 0;
}
