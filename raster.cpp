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
    const void*         Data;
    const uint16_t*     Indices;
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

Color srSampleTextureLod(int sx, int sy, float u, float v, float mipLevel)
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

    int mip = (int)(mipLevel + 0.5);
    uint32_t mipWidth = max(1u, texture.Width >> mip);
    uint32_t mipHeight = max(1u, texture.Height >> mip);

    int x = (int)(u * mipWidth);
    int y = (int)(v * mipHeight);

    Color* mipData = (Color*)(((uint8_t*)texture.Data) + texture.MipOffsets[mip]);
    return mipData[y * mipWidth + x];
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

    int cy0 = Edge(x1, y1, x2, y2, bounds[0], bounds[1]);
    int cy1 = Edge(x2, y2, x0, y0, bounds[0], bounds[1]);
    int cy2 = Edge(x0, y0, x1, y1, bounds[0], bounds[1]);

    float* depthBuffer = (float*)Ctx.Out.DB;

    for (int y = bounds[1]; y <= bounds[3]; y += 2)
    {
        int cx0Row = cy0;
        int cx1Row = cy1;
        int cx2Row = cy2;

        for (int x = bounds[0]; x <= bounds[2]; x += 2)
        {
            int cx0[4] = { cx0Row, cx0Row + A12, cx0Row + B12, cx0Row + A12 + B12 };
            int cx1[4] = { cx1Row, cx1Row + A20, cx1Row + B20, cx1Row + A20 + B20 };
            int cx2[4] = { cx2Row, cx2Row + A01, cx2Row + B01, cx2Row + A01 + B01 };

            float w0[4], w1[4], w2[4];
            float zndc[4], u[4], v[4];
            int   px[4], py[4];

            // precompute interpolants for quad
            for (int i = 0; i < 4; ++i)
            {
                px[i] = x + (i & 1);
                py[i] = y + (i >> 1);

                w0[i] = cx0[i] * invArea;
                w1[i] = cx1[i] * invArea;
                w2[i] = cx2[i] * invArea;

                float depth = 1.0f / (w0[i] * v0->InvW + w1[i] * v1->InvW + w2[i] * v2->InvW);
                zndc[i] = (w0[i] * v0->ZOverW + w1[i] * v1->ZOverW + w2[i] * v2->ZOverW) * depth;
                u[i]    = (w0[i] * v0->UOverW + w1[i] * v1->UOverW + w2[i] * v2->UOverW) * depth;
                v[i]    = (w0[i] * v0->VOverW + w1[i] * v1->VOverW + w2[i] * v2->VOverW) * depth;
            }

            // calculate u/v derivatives and mip-level
            float dudx = ((u[1] + u[3]) - (u[0] + u[2])) * 0.5f;
            float dudy = ((u[2] + u[3]) - (u[0] + u[1])) * 0.5f;
            float dvdx = ((v[1] + v[3]) - (v[0] + v[2])) * 0.5f;
            float dvdy = ((v[2] + v[3]) - (v[0] + v[1])) * 0.5f;

            float dudx2 = dudx * dudx;
            float dudy2 = dudy * dudy;
            float dvdx2 = dvdx * dvdx;
            float dvdy2 = dvdy * dvdy;

            float rho2 = fmaxf(dudx2 + dvdx2, dudy2 + dvdy2);
            float mipLevel = 0.5f * Log2Fast(rho2 * Ctx.Texture.Width * Ctx.Texture.Width);
            mipLevel = Clamp(mipLevel, 0.0f, (float)Ctx.Texture.MipLevels - 1);

            for (int i = 0; i < 4; ++i)
            {
                if (cx0[i] >= 0 && cx1[i] >= 0 && cx2[i] >= 0)
                {
                    if (zndc[i] < depthBuffer[py[i] * FB_WIDTH + px[i]])
                    {
                        depthBuffer[py[i] * FB_WIDTH + px[i]] = zndc[i];
                        Color color = srSampleTextureLod(px[i], py[i], u[i], v[i], mipLevel);
                        srDrawPixel(px[i], py[i], color);
                    }
                }
            }

            cx0Row += 2 * A12;
            cx1Row += 2 * A20;
            cx2Row += 2 * A01;
        }

        cy0 += 2 * B12;
        cy1 += 2 * B20;
        cy2 += 2 * B01;
    }
}

static void RunVertexTransform(int start, int end, void* context)
{
    struct Region* region;
    size_t offset;
    const int exportReserveSize = (end - start) * sizeof(ExportVertex) * 3;
    ExportVertex* exportVertexPtr = (ExportVertex*)ExportBufferReserve(ExportBuffer, exportReserveSize, 1, &offset, &region);

    // setup vertex attribute streams
    VertexTransformCommand* command = (VertexTransformCommand*)context;
    int stride = srInputStreamElementSize(command->Elements, command->NumInputElements);

    const InputElement* inputElementPosition = srInputStreamElementByType(command->Elements, command->NumInputElements, InputElementType::TypePosition);
    assert(inputElementPosition != NULL);

    const InputElement* inputElementTexcoord = srInputStreamElementByType(command->Elements, command->NumInputElements, InputElementType::TypeTexcoord);
    assert(inputElementTexcoord != NULL);

    for (int index = start; index < end; ++index)
    {
        float* pos0 = (float*)srInputStreamElement(command->Data, *inputElementPosition, stride, command->Indices[index * 3 + 0]);
        float* pos1 = (float*)srInputStreamElement(command->Data, *inputElementPosition, stride, command->Indices[index * 3 + 1]);
        float* pos2 = (float*)srInputStreamElement(command->Data, *inputElementPosition, stride, command->Indices[index * 3 + 2]);

        float* tex[3];
        tex[0] = (float*)srInputStreamElement(command->Data, *inputElementTexcoord, stride, command->Indices[index * 3 + 0]);
        tex[1] = (float*)srInputStreamElement(command->Data, *inputElementTexcoord, stride, command->Indices[index * 3 + 1]);
        tex[2] = (float*)srInputStreamElement(command->Data, *inputElementTexcoord, stride, command->Indices[index * 3 + 2]);

        vec4 pos[3];
        Matrix4MulVec3(command->ProjectionMatrix, pos0, 1, pos[0]);
        Matrix4MulVec3(command->ProjectionMatrix, pos1, 1, pos[1]);
        Matrix4MulVec3(command->ProjectionMatrix, pos2, 1, pos[2]);

        // FIXME: clip triangle against frustrum

        ClipToScreen(pos[0], FB_WIDTH, FB_HEIGHT, pos[0]);
        ClipToScreen(pos[1], FB_WIDTH, FB_HEIGHT, pos[1]);
        ClipToScreen(pos[2], FB_WIDTH, FB_HEIGHT, pos[2]);

        for (int j = 0; j < 3; ++j)
        {
            ExportVertex* vert = exportVertexPtr++;
            float invW = 1.0f / pos[j][3];
            vert->ScreenX = (int)pos[j][0];
            vert->ScreenY = (int)pos[j][1];
            vert->InvW = invW;
            vert->ZOverW = pos[j][2] * invW;
            vert->UOverW = tex[j][0] * invW;
            vert->VOverW = tex[j][1] * invW;
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

void srDrawTriangleList(const void* data, const uint16_t* indices, const InputElement* elements, int numInputElements, int numPrimitives, mat4 ProjectionMatrix, bool parallel)
{
    VertexTransformCommand command = {
        .Data = data,
        .Indices = indices,
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
    assert(numElements >= 1);

    uint32_t maxOffset = elements[0].Offset;
    InputElementFormat lastElementFormat = elements[0].Format;

    for (int i = 1; i < numElements; ++i)
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
