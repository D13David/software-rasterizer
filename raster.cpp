#include "raster.h"
#include "mathlib_intrinsics.h"
#include "texture_loader.h"
#include "thread_pool.h"
#include "parallel_for.h"
#include "export_buffer.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

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

typedef struct VertexTransformCommand
{
    const void* Data;
    const InputElement* Elements;
    int                 NumInputElements;
    mat4                ProjectionMatrix;
} VertexTransformCommand;

#define FB_WIDTH    Ctx.Out.Width
#define FB_HEIGHT   Ctx.Out.Height

#define THREAD_GROUP_SIZE  128
#define EXPORT_BUFFER_SIZE (32 * 1024 * 1024)

RasterContext Ctx;
ExportBufferHandle ExportBuffer;
ThreadPoolHandle ThreadPool;

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

    size_t regionSize = THREAD_GROUP_SIZE * sizeof(ExportVertex) * 3;
    size_t maxRegions = (EXPORT_BUFFER_SIZE + regionSize - 1) / regionSize;
    ExportBuffer = ExportBufferCreate(EXPORT_BUFFER_SIZE, maxRegions);
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
    int bufferSize = FB_WIDTH * FB_HEIGHT;

    Color* colorBuffer = (Color*)Ctx.Out.CB;
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
    ((Color*)Ctx.Out.CB)[y * FB_WIDTH + x] = color;
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
    out[0] = max(minx, 0);
    out[1] = max(miny, 0);
    out[2] = min(maxx, FB_WIDTH - 1);
    out[3] = min(maxy, FB_HEIGHT - 1);
}

Color srSampleTexture(int sx, int sy, float u, float v)
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
    
    int x = (int)(u * texture.Width);
    int y = (int)(v * texture.Height);

    return ((Color*)texture.Data)[y * texture.Width + x];
}

static void srDrawTriangle(const ExportVertex* v0, const ExportVertex* v1, const ExportVertex* v2)
{
    int x0 = v0->ScreenX, y0 = v0->ScreenY;
    int x1 = v1->ScreenX, y1 = v1->ScreenY;
    int x2 = v2->ScreenX, y2 = v2->ScreenY;

    int area = Edge(x0, y0, x1, y1, x2, y2);
    if (area < 0) {
        return;
    }

    float invArea = 1.0f / area;
    vec4i bounds;
    ComputeAABB(x0, y0, x1, y1, x2, y2, bounds);

    int A01 = y0 - y1, B01 = x1 - x0;
    int A12 = y1 - y2, B12 = x2 - x1;
    int A20 = y2 - y0, B20 = x0 - x2;

    /*int px = bounds[0] + 0.5f;
    int py = bounds[1] + 0.5f;*/

    int cy0 = Edge(x1, y1, x2, y2, bounds[0], bounds[1]);
    int cy1 = Edge(x2, y2, x0, y0, bounds[0], bounds[1]);
    int cy2 = Edge(x0, y0, x1, y1, bounds[0], bounds[1]);

    for (int y = bounds[1]; y <= bounds[3]; ++y)
    {
        int cx0 = cy0;
        int cx1 = cy1;
        int cx2 = cy2;

        for (int x = bounds[0]; x <= bounds[2]; ++x)
        {
            if (cx0 >= 0 && cx1 >= 0 && cx2 >= 0)
            {
                float w0 = cx0 * invArea;
                float w1 = cx1 * invArea;
                float w2 = cx2 * invArea;

                float depth = 1.0f / (w0 * v0->InvW + w1 * v1->InvW + w2 * v2->InvW);

                float z_ndc = (w0 * v0->ZOverW + 
                               w1 * v1->ZOverW + 
                               w2 * v2->ZOverW) * depth;

                float* depthBuffer = (float*)Ctx.Out.DB;
                if (z_ndc < depthBuffer[y * FB_WIDTH + x])
                {
                    depthBuffer[y * FB_WIDTH + x] = z_ndc;

                    float u = (w0 * v0->UOverW +
                               w1 * v1->UOverW +
                               w2 * v2->UOverW) * depth;

                    float v = (w0 * v0->VOverW +
                               w1 * v1->VOverW +
                               w2 * v2->VOverW) * depth;

                    Color Color = srSampleTexture(x, y, u, v);

                    // interpolate Color over triangle
                    /*float r = w0 * v0->r + w1 * v1->r + w2 * v2->r;
                    float g = w0 * v0->g + w1 * v1->g + w2 * v2->g;
                    float b = w0 * v0->b + w1 * v1->b + w2 * v2->b;*/

                    srDrawPixel(x, y, Color);
                }
            }

            cx0 += A12;
            cx1 += A20;
            cx2 += A01;
        }

        cy0 += B12;
        cy1 += B20;
        cy2 += B01;
    }
}

static void RunVertexTransform(int start, int end, void* context)
{
    struct Region* region;
    size_t offset;
    const int exportReserveSize = (end - start) * sizeof(ExportVertex) * 3;
    ExportVertex* exportVertexPtr = (ExportVertex*)ExportBufferReserve(ExportBuffer, exportReserveSize, 1, &offset, &region);

    for (int index = start; index < end; ++index)
    {
        VertexTransformCommand* command = (VertexTransformCommand*)context;

        int stride = srInputStreamElementSize(command->Elements, command->NumInputElements);

        const InputElement* inputElementPosition = srInputStreamElementByType(command->Elements, command->NumInputElements, InputElementType::TypePosition);
        assert(inputElementPosition != NULL);
        float* pos0 = (float*)srInputStreamElement(command->Data, *inputElementPosition, stride, index * 3 + 0);
        float* pos1 = (float*)srInputStreamElement(command->Data, *inputElementPosition, stride, index * 3 + 1);
        float* pos2 = (float*)srInputStreamElement(command->Data, *inputElementPosition, stride, index * 3 + 2);

        vec4 pos[3];
        Matrix4MulVec3(command->ProjectionMatrix, pos0, 1, pos[0]);
        Matrix4MulVec3(command->ProjectionMatrix, pos1, 1, pos[1]);
        Matrix4MulVec3(command->ProjectionMatrix, pos2, 1, pos[2]);

        // FIXME: clip triangle against frustrum

        ClipToScreen(pos[0], FB_WIDTH, FB_HEIGHT, pos[0]);
        ClipToScreen(pos[1], FB_WIDTH, FB_HEIGHT, pos[1]);
        ClipToScreen(pos[2], FB_WIDTH, FB_HEIGHT, pos[2]);

        const InputElement* inputElementTexcoord = srInputStreamElementByType(command->Elements, command->NumInputElements, InputElementType::TypeTexcoord);
        assert(inputElementTexcoord != NULL);
        const void* uv0 = srInputStreamElement(command->Data, *inputElementTexcoord, stride, index * 3 + 0);
        const void* uv1 = srInputStreamElement(command->Data, *inputElementTexcoord, stride, index * 3 + 1);
        const void* uv2 = srInputStreamElement(command->Data, *inputElementTexcoord, stride, index * 3 + 2);

        const float* t[3]{ (float*)(uv0),
                           (float*)(uv1),
                           (float*)(uv2) };

        for (int j = 0; j < 3; ++j)
        {
            ExportVertex* vert = exportVertexPtr++;
            float invW = 1.0f / pos[j][3];
            vert->ScreenX = (int)pos[j][0];
            vert->ScreenY = (int)pos[j][1];
            vert->InvW = invW;
            vert->ZOverW = pos[j][2] * invW;
            vert->UOverW = t[j][0] * invW;
            vert->VOverW = t[j][1] * invW;
        }
    }

    // publish the written region
    ExportBufferPublish(ExportBuffer, region);
}

static void RunRasterizeTriangles(int start, int end, void* context)
{
    ExportVertex* transformedVertices = (ExportVertex*)ExportBufferData(ExportBuffer) + start * 3;

    for (int i = 0; i < (end - start); ++i)
    {
        srDrawTriangle(&transformedVertices[0], &transformedVertices[1], &transformedVertices[2]);
        transformedVertices += 3;
    }
}

void srDrawTriangleList(const void* data, const InputElement* elements, int numInputElements, int numPrimitives, mat4 ProjectionMatrix, bool parallel)
{
    VertexTransformCommand command = {
        .Data = data,
        .Elements = elements,
        .NumInputElements = numInputElements
    };
    CopyMatrix(ProjectionMatrix, command.ProjectionMatrix);
    if (parallel)
    {
        ParallelFor(ThreadPool, 0, numPrimitives, THREAD_GROUP_SIZE, &RunVertexTransform, &command);

        // TODO: this can actually start in parallel to the vertex transformation
        size_t numVerticesWritten = ExportBufferUsed(ExportBuffer) / sizeof(ExportVertex);
        ParallelFor(ThreadPool, 0, numVerticesWritten * 3, THREAD_GROUP_SIZE, &RunRasterizeTriangles, NULL);
    }
    else
    {
        RunVertexTransform(0, numPrimitives, &command);

        size_t numVerticesWritten = ExportBufferUsed(ExportBuffer) / sizeof(ExportVertex);
        RunRasterizeTriangles(0, numVerticesWritten, NULL);
    }

    // TODO: we probably don't need to reset the buffer here
    ExportBufferReset(ExportBuffer);
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

    uint32_t maxOffset = 0;
    InputElementFormat lastElementFormat;
    for (int i = 0; i < numElements; ++i)
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
