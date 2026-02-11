#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <stdlib.h>

#include "mathlib.h"
#include "mesh_loader.h"
#include "raster.h"
#include "fps_meter.h"

#define FPS_METER_WIDTH 200
#define FPS_METER_HEIGHT 70
#define FB_WIDTH 1280
#define FB_HEIGHT 720

static HBITMAP  hBitmap = nullptr;
static HDC      hMemDC = nullptr;
static HWND     hWnd = nullptr;
void*           FrameBuffer;
void*           DepthBuffer;
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


LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void Initialize(HINSTANCE hInst)
{
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"DIBFramebufferClass";

    RegisterClass(&wc);

    hWnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        L"DIB Section Framebuffer",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        FB_WIDTH + 16, FB_HEIGHT + 39,
        NULL, NULL,
        hInst,
        NULL
    );
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = FB_WIDTH;
        bmi.bmiHeader.biHeight = -FB_HEIGHT; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC hdc = GetDC(hwnd);

        hBitmap = CreateDIBSection(
            hdc,
            &bmi,
            DIB_RGB_COLORS,
            (void**)&FrameBuffer,
            NULL,
            0
        );

        hMemDC = CreateCompatibleDC(hdc);
        SelectObject(hMemDC, hBitmap);
        ReleaseDC(hwnd, hdc);

        DepthBuffer = (float*)malloc(FB_WIDTH * FB_HEIGHT * sizeof(float));

        srInitialize({
            .BufferDesc = {
                .Width = FB_WIDTH,
                .Height = FB_HEIGHT
            },
            .FrameBufferPtr = FrameBuffer,
            .DepthBufferPtr = DepthBuffer

        });

        return 0;
    }

    case WM_DESTROY:
        DeleteDC(hMemDC);
        DeleteObject(hBitmap);
        PostQuitMessage(0);
        free(DepthBuffer);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
    Initialize(GetModuleHandleA(NULL));

    if (!LoadMeshFromFile("./meshes/Base mesh.obj", &Gmesh)) 
    {
        MessageBox(NULL, L"Failed to load mesh", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    FPSMeterInitialize();

    while (true)
    {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) {
                goto exit;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        
        DrawFrame();

        HDC hdc = GetDC(hWnd);
        BitBlt(hdc, 0, 0, FB_WIDTH, FB_HEIGHT, hMemDC, 0, 0, SRCCOPY);
        ReleaseDC(hWnd, hdc);
    }
exit:
    return 0;
}
