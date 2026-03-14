#include "raster.h"
#include "raster_internal.h"
#include "common/export_buffer.h"
#include "render_stats.h"
#include "common/profile.h"
#include "common/thread_pool.h"
#include "common/parallel_for.h"

typedef struct ClipVertex
{
    vec4    ClipSpacePos;
    vec2    TexCoords;
    vec3    Color;
    vec3    Normal;
} ClipVertex;

ExportBufferHandle ExportBuffer;

static PJD_INLINE uint8_t VertexOutcode(vec4 p)
{
    uint8_t outcode = 0;
    if (p[0] < -p[3]) outcode |= (1 << 0);
    if (p[0] >  p[3]) outcode |= (1 << 1);
    if (p[1] < -p[3]) outcode |= (1 << 2);
    if (p[1] >  p[3]) outcode |= (1 << 3);
    if (p[2] < -p[3]) outcode |= (1 << 4);
    if (p[2] >  p[3]) outcode |= (1 << 5);
    return outcode;
}

static PJD_INLINE ClipVertex Clip(const ClipVertex* v0, const ClipVertex* v1, float dot0, float dot1)
{
    ClipVertex out;
    float alpha = dot0 / (dot0 - dot1);
    LerpVec4(v0->ClipSpacePos, v1->ClipSpacePos, alpha, out.ClipSpacePos);
    LerpVec2(v0->TexCoords, v1->TexCoords, alpha, out.TexCoords);
    LerpVec3(v0->Color, v1->Color, alpha, out.Color);
    LerpVec3(v0->Normal, v1->Normal, alpha, out.Normal);
    return out;
}

static int ClipPolygonAgainstPlane(const vec4 plane, const ClipVertex* verts, int numVertices, ClipVertex* clippedVerts)
{
    assert(numVertices != 0);

    ClipVertex prev = verts[numVertices - 1];
    float prevDist = Vec4Dot(prev.ClipSpacePos, plane);
    int prevInside = prevDist >= 0.0f;
    int count = 0;

    for (int i = 0; i < numVertices; ++i)
    {
        ClipVertex curr = verts[i];
        float currDist = Vec4Dot(curr.ClipSpacePos, plane);
        int currInside = currDist >= 0.0f;

        if (currInside)
        {
            if (!prevInside) {
                clippedVerts[count++] = Clip(&prev, &curr, prevDist, currDist);
            }
            clippedVerts[count++] = curr;
        }
        else if (prevInside)
        {
            clippedVerts[count++] = Clip(&prev, &curr, prevDist, currDist);
        }

        prev = curr;
        prevInside = currInside;
        prevDist = currDist;
    }

    return count;
}

static int ClipTriangleAgainstFrustum(ClipVertex verts[3], uint8_t m0, uint8_t m1, uint8_t m2, ClipVertex clippedVerts[12])
{
    // assume only intersecting triangles are processed here
    assert((m0 & m1 & m2) == 0 && (m0 | m1 | m2) != 0);

    static const vec4 FrustumPlanes[6] =
    {
        {  1,  0,  0,  1 }, // Left
        { -1,  0,  0,  1 }, // Right
        {  0,  1,  0,  1 }, // Bottom
        {  0, -1,  0,  1 }, // Top
        {  0,  0,  1,  1 }, // Near
        {  0,  0, -1,  1 }  // Far
    };

    ClipVertex buffer[2][12];

    memcpy(buffer[0], verts, sizeof(ClipVertex) * 3);

    int count = 3;
    int source = 0, target = 1;
    uint8_t mask = (m0 | m1 | m2);

    for (int i = 0; i < 6; ++i)
    {
        if (!(mask & (1 << i))) {
            continue;
        }

        count = ClipPolygonAgainstPlane(FrustumPlanes[i], buffer[source], count, buffer[target]);
        if (count == 0) {
            return 0;
        }

        // pingpong index update
        target = 1 - target, source = 1 - source;
    }

    memcpy(clippedVerts, buffer[source], sizeof(ClipVertex) * count);

    return count;
}

static vec2 TexCoordDefault = { 0, 0 };
static vec3 ColorDefault = { 0, 0, 0 };
static vec3 NormalDefault = { 0, 0, 0 };

static void RunVertexTransform_(size_t id, int start, int end, void* context)
{
    // setup vertex attribute streams
    VertexTransformCommand* command = (VertexTransformCommand*)context;
    size_t stride = InputStreamStride(command->Elements, command->NumInputElements);

    const InputElementDescriptor* inputElementPosition = InputStreamElementByType(command->Elements, command->NumInputElements, InputElementType::Position);
    assert(inputElementPosition != NULL);

    const InputElementDescriptor* inputElementTexcoord = InputStreamElementByType(command->Elements, command->NumInputElements, InputElementType::Texcoord);
    const InputElementDescriptor* inputElementColor    = InputStreamElementByType(command->Elements, command->NumInputElements, InputElementType::Color);
    const InputElementDescriptor* inputElementNormal   = InputStreamElementByType(command->Elements, command->NumInputElements, InputElementType::Normal);

    int index = start;
    int allocationHint = 0;

    do {
        // allocate a chunk of memory for our transformed vertex exports
        int allocatedVertexCount = (allocationHint > 0 ? allocationHint : (end - index)) * 3;
        const Range* range = ExportBufferReserve(ExportBuffer, allocatedVertexCount * sizeof(ExportVertex));
        ExportVertex* exportVertexPtr = (ExportVertex*)range->Ptr;
        assert(exportVertexPtr != NULL);
        ExportVertex* exportVertexEndPtr = exportVertexPtr + allocatedVertexCount;

        allocationHint = 0;

        for (; index < end && exportVertexPtr < exportVertexEndPtr; ++index)
        {
            float* pos0 = (float*)InputStreamElementPtr(command->Data, inputElementPosition, stride, command->Indices[index * 3 + 0]);
            float* pos1 = (float*)InputStreamElementPtr(command->Data, inputElementPosition, stride, command->Indices[index * 3 + 1]);
            float* pos2 = (float*)InputStreamElementPtr(command->Data, inputElementPosition, stride, command->Indices[index * 3 + 2]);

            vec4 pos[3];
            Matrix4MulVec3(command->ProjectionMatrix, pos0, 1, pos[0]);
            Matrix4MulVec3(command->ProjectionMatrix, pos1, 1, pos[1]);
            Matrix4MulVec3(command->ProjectionMatrix, pos2, 1, pos[2]);

            // compute vertex outcode for trivial reject
            uint8_t out0 = VertexOutcode(pos[0]);
            uint8_t out1 = VertexOutcode(pos[1]);
            uint8_t out2 = VertexOutcode(pos[2]);

            // triangle is fully outside, we can skip it
            if ((out0 & out1 & out2) != 0)
            {
                RENDER_STATS_ADD(TrianglesCulled, 1);
                memset(exportVertexPtr, 0, sizeof(ExportVertex) * 3);
                exportVertexPtr += 3;
                continue;
            }

            float *tex[3]  = { TexCoordDefault, TexCoordDefault, TexCoordDefault },
                  *col[3]  = { ColorDefault, ColorDefault, ColorDefault },
                  *norm[3] = { NormalDefault, NormalDefault, NormalDefault };

            if (inputElementTexcoord) {
                tex[0] = (float*)InputStreamElementPtr(command->Data, inputElementTexcoord, stride, command->Indices[index * 3 + 0]);
                tex[1] = (float*)InputStreamElementPtr(command->Data, inputElementTexcoord, stride, command->Indices[index * 3 + 1]);
                tex[2] = (float*)InputStreamElementPtr(command->Data, inputElementTexcoord, stride, command->Indices[index * 3 + 2]);
            }
            if (inputElementColor) {
                col[0] = (float*)InputStreamElementPtr(command->Data, inputElementColor, stride, command->Indices[index * 3 + 0]);
                col[1] = (float*)InputStreamElementPtr(command->Data, inputElementColor, stride, command->Indices[index * 3 + 1]);
                col[2] = (float*)InputStreamElementPtr(command->Data, inputElementColor, stride, command->Indices[index * 3 + 2]);
            }
            if (inputElementNormal) {
                norm[0] = (float*)InputStreamElementPtr(command->Data, inputElementNormal, stride, command->Indices[index * 3 + 0]);
                norm[1] = (float*)InputStreamElementPtr(command->Data, inputElementNormal, stride, command->Indices[index * 3 + 1]);
                norm[2] = (float*)InputStreamElementPtr(command->Data, inputElementNormal, stride, command->Indices[index * 3 + 2]);
            }

            ClipVertex input[3], clipped[12];
            int numClippedVertices = 0;
            if ((out0 | out1 | out2) != 0)
            {
                RENDER_STATS_ADD(TrianglesClipped, 1);

                // estimate upper bound of triangles after clipping and triangulating
                int maxVertices = (min(12, 3 + __popcnt(out0 | out1 | out2)) - 2) * 3;

                // not enough buffer space remaining to hold all triangles, start over
                ptrdiff_t numFreeVertices = exportVertexEndPtr - exportVertexPtr;
                if (numFreeVertices < maxVertices)
                {
                    allocationHint = maxVertices;
                    break;
                }

                // enough space left for export, then clip triangle
                for (int i = 0; i < 3; ++i)
                {
                    Vec4Copy(pos[i], input[i].ClipSpacePos);
                    Vec2Copy(tex[i], input[i].TexCoords);
                    Vec3Copy(col[i], input[i].Color);
                    Vec3Copy(norm[i], input[i].Normal);
                }

                numClippedVertices = ClipTriangleAgainstFrustum(input, out0, out1, out2, clipped);
            }
            else
            {
                // fully inside, no clipping needed
                for (int i = 0; i < 3; ++i)
                {
                    Vec4Copy(pos[i], clipped[i].ClipSpacePos);
                    Vec2Copy(tex[i], clipped[i].TexCoords);
                    Vec3Copy(col[i], clipped[i].Color);
                    Vec3Copy(norm[i], clipped[i].Normal);
                }
                numClippedVertices = 3;
            }

            ClipVertex v0 = clipped[0];
            ClipToScreen(v0.ClipSpacePos, FB_WIDTH - 1, FB_HEIGHT - 1, v0.ClipSpacePos);

            for (int i = 1; i < numClippedVertices - 1; ++i)
            {
                ClipVertex v1 = clipped[i];
                ClipToScreen(v1.ClipSpacePos, FB_WIDTH - 1, FB_HEIGHT - 1, v1.ClipSpacePos);

                ClipVertex v2 = clipped[i + 1];
                ClipToScreen(v2.ClipSpacePos, FB_WIDTH - 1, FB_HEIGHT - 1, v2.ClipSpacePos);

#define EXPORT_VERTEX(idx, vert) do {                                   \
        float invW = vert.ClipSpacePos[3];                              \
        exportVertexPtr[(idx)].ScreenX = (int)vert.ClipSpacePos[0];     \
        exportVertexPtr[(idx)].ScreenY = (int)vert.ClipSpacePos[1];     \
        exportVertexPtr[(idx)].InvW    = invW;                          \
        exportVertexPtr[(idx)].Z       = vert.ClipSpacePos[2];          \
        exportVertexPtr[(idx)].UOverW  = vert.TexCoords[0] * invW;      \
        exportVertexPtr[(idx)].VOverW  = vert.TexCoords[1] * invW;      \
        Vec3Copy(vert.Color, exportVertexPtr[(idx)].Color);             \
        Vec3Copy(vert.Normal, exportVertexPtr[(idx)].Normal);           \
    } while (0)

                EXPORT_VERTEX(0, v0);
                EXPORT_VERTEX(1, v1);
                EXPORT_VERTEX(2, v2);

                exportVertexPtr += 3;

                RENDER_STATS_ADD(TrianglesRendered, 1);
            }
        }

        // publish the written range
        ExportBufferPublish(ExportBuffer, range);

    } while (index < end); // // previously allocated range is full but still vertices left to transform.. let's start over
}

void RunVertexTransform(bool parallelize, int numPrimitives, VertexTransformCommand* command)
{
    PROFILE_AUTO("Vertex Transform");
    if (parallelize) ParallelFor(ThreadPool, 0, numPrimitives, THREAD_GROUP_SIZE_VTRANSFORM, &RunVertexTransform_, true, command);
    else RunVertexTransform_(0, 0, numPrimitives, command);
}