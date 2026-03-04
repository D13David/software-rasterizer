#include "mesh.h"
#include "raster.h"

Mesh* MeshCreate(uint8_t vertexSize, uint32_t numVertices, uint32_t numSurfaces)
{
    Mesh* mesh = (Mesh*)calloc(1, sizeof(Mesh));
    if (mesh == NULL) {
        return NULL;
    }

    mesh->VertexBuffer = malloc(vertexSize * numVertices);
    assert(mesh->VertexBuffer);
    mesh->NumVertices = numVertices;

    mesh->Surfaces = (Surface*)calloc(numSurfaces, sizeof(Surface));
    assert(mesh->Surfaces);
    mesh->NumSurfaces = numSurfaces;

    mesh->AnimSeqs = NULL;
    mesh->NumAnimSeqs = 0;

    return mesh;
}

void MeshFree(Mesh* mesh)
{
    if (mesh == NULL) {
        return;
    }

    if (mesh->VertexBuffer) free(mesh->VertexBuffer);
    if (mesh->Surfaces)
    {
        for (int i = 0; i < mesh->NumSurfaces; ++i) {
            free(mesh->Surfaces[i].IndexBuffer);
        }
    }
    if (mesh->AnimSeqs) free(mesh->AnimSeqs);
    if (mesh->FrameCache) free(mesh->FrameCache);

    free(mesh);
}

const MeshAnimSeq* FindAnimSequence(Mesh* mesh, const char* name)
{
    for (int i = 0; i < mesh->NumAnimSeqs; ++i)
    {
        if (!strcmp(mesh->AnimSeqs[i].Name, name)) {
            return &mesh->AnimSeqs[i];
        }
    }
    return NULL;
}

static void UpdateFramesExplicite(Mesh* mesh, int frameOffset1, int frameOffset2, float blendFactor)
{
    int vertexSize = InputStreamElementSize(mesh->InputDesc, mesh->NumInputElements);

    float* uvReference = (float*)mesh->VertexBuffer;
    float* frame1Start = (float*)((uint8_t*)mesh->VertexBuffer + (frameOffset1 * vertexSize));
    float* frame2Start = (float*)((uint8_t*)mesh->VertexBuffer + (frameOffset2 * vertexSize));

    if (!mesh->FrameCache) {
        mesh->FrameCache = malloc(mesh->NumVertsPerFrame * vertexSize);
    }
    float* target = (float*)mesh->FrameCache;

    for (int i = 0; i < mesh->NumVertsPerFrame; ++i)
    {
        // FIXME: proper lookup, this assumes position location
        float x1, y1, z1, x2, y2, z2;
        x1 = frame1Start[0]; y1 = frame1Start[1]; z1 = frame1Start[2];
        x2 = frame2Start[0]; y2 = frame2Start[1]; z2 = frame2Start[2];

        target[0] = x1 + (x2 - x1) * blendFactor;
        target[1] = y1 + (y2 - y1) * blendFactor;
        target[2] = z1 + (z2 - z1) * blendFactor;
        target[3] = uvReference[3];
        target[4] = uvReference[4];

        uvReference += vertexSize / sizeof(float);
        frame1Start += vertexSize / sizeof(float);
        frame2Start += vertexSize / sizeof(float);
        target += vertexSize / sizeof(float);
    }
}

void UpdateFrame(Mesh* mesh, const MeshAnimSeq* anim, float frame)
{
    int firstFrame = (int)frame;
    int offset1 = (anim->StartFrame + ((firstFrame + 0) % anim->NumFrames)) * mesh->NumVertsPerFrame;
    int offset2 = (anim->StartFrame + ((firstFrame + 1) % anim->NumFrames)) * mesh->NumVertsPerFrame;
    UpdateFramesExplicite(mesh, offset1, offset2, frame - firstFrame);
}

void UpdateFrameBlendAnims(Mesh* mesh, const MeshAnimSeq* anim1, const MeshAnimSeq* anim2, int frame1, int frame2, float blendFactor)
{
    int offset1 = (anim1->StartFrame + (frame1 % anim1->NumFrames)) * mesh->NumVertsPerFrame;
    int offset2 = (anim2->StartFrame + (frame2 % anim2->NumFrames)) * mesh->NumVertsPerFrame;
    UpdateFramesExplicite(mesh, offset1, offset2, blendFactor);
}
