#ifndef PJD_MESH_H
#define PJD_MESH_H

#include "raster_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Surface
{
    uint32_t*   IndexBuffer;
    uint16_t    NumPrimitives;
    TextureView Texture;
} Surface;

typedef struct MeshAnimSeq
{
    char        Name[16];
    uint32_t    StartFrame;
    uint32_t    NumFrames;
    float       Rate;
} MeshAnimSeq;

typedef struct Mesh 
{
    void*        VertexBuffer;
    uint32_t     NumVertices;
    Surface*     Surfaces;
    uint16_t     NumSurfaces;
    MeshAnimSeq* AnimSeqs;
    uint16_t     NumAnimSeqs;

    // FIXME: detach this from mesh
    InputElementDescriptor InputDesc[3];
    int          NumInputElements;

    uint32_t     NumVertsPerFrame;
    void*        FrameCache;
} Mesh;

Mesh* MeshCreate(uint8_t vertexSize, uint32_t numVertices, uint32_t numSurfaces);
void MeshFree(Mesh* mesh);
const MeshAnimSeq* FindAnimSequence(Mesh* mesh, const char* name);
void UpdateFrame(Mesh* mesh, const MeshAnimSeq* anim, float frame);
void UpdateFrameBlendAnims(Mesh* mesh, const MeshAnimSeq* anim1, const MeshAnimSeq* anim2, int frame1, int frame2, float blendFactor);

#ifdef __cplusplus
}
#endif

#endif // PJD_MESH_H