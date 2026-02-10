#ifndef PJD_MESH_LOADER_H
#define PJD_MESH_LOADER_H

#include "raster.h"

typedef struct Mesh
{
    float* verts;
    int numTris;
    InputElement inputDesc[3];
    int numInputElements;
    TextureView texture;
} Mesh;

int GenerateMeshTriangle(Mesh* mesh);
int GenerateMeshQuad(Mesh* mesh);
int LoadMeshFromFile(const char* filename, Mesh* mesh);

#endif // PJD_MESH_LOADER_H