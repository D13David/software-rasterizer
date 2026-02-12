#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <stdlib.h>

#include "thirteen.h"

#include "mathlib.h"
#include "mesh_loader.h"
#include "raster.h"
#include "fps_meter.h"

#define FPS_METER_WIDTH 200
#define FPS_METER_HEIGHT 70
#define FB_WIDTH 1280
#define FB_HEIGHT 720

Mesh Gmesh, Gmesh1;

static void DrawMesh(const Mesh* mesh, float tx, float ty, float tz)
{
    // object transform
    mat4 ObjectTransform;
    CreateMatrixTransform(tx, ty, tz, ObjectTransform);

    static float angle = 180;
    angle += 0.05f;
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
    CreateMatrixPerspectiveFovLH(60.0f, FB_WIDTH / (float)FB_HEIGHT, 0.1f, 200.0f, ProjectionMat);
    
    // world view projection
    mat4 WorldViewProj;
    Matrix4Mul(WorldMat, ProjectionMat, WorldViewProj);

    srSetTextureView(mesh->texture);

    srDrawTriangleList(Gmesh.verts, Gmesh.inputDesc, Gmesh.numInputElements, Gmesh.numTris, WorldViewProj, true);
}

static void DrawFrame()
{
    srClear(RGB(0, 0, 0));

    srSetTextureFilter(TextureFilter::Unreal);
    DrawMesh(&Gmesh, 0, 0, 20);

    /*srSetTextureFilter(TextureFilter::Unreal);
    DrawMesh(&Gmesh1, 1, 0, 1.2);*/

    FPSMeterUpdate();
    FPSMeterDraw(0, FB_HEIGHT - 1 - FPS_METER_HEIGHT, FPS_METER_WIDTH, FPS_METER_HEIGHT);
}

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
    uint8_t* frameBuffer = Thirteen::Init(FB_WIDTH, FB_HEIGHT);

    float* depthBuffer = (float*)malloc(FB_WIDTH * FB_HEIGHT * sizeof(float));

    srInitialize({
            .BufferDesc = {
                .Width = FB_WIDTH,
                .Height = FB_HEIGHT
            },
            .FrameBufferPtr = frameBuffer,
            .DepthBufferPtr = depthBuffer

        });

    if (!LoadMeshFromFile("./meshes/Base mesh.obj", &Gmesh)) 
    {
        MessageBox(NULL, L"Failed to load mesh", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    FPSMeterInitialize();

    while (true)
    {
        DrawFrame();

        if (!Thirteen::Render()) {
            break;
        }
    }
exit:
    if (depthBuffer) {
        free(depthBuffer);
    }
    Thirteen::Shutdown();

    return 0;
}
