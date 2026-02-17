#ifndef PJD_MESH_LOADER_H
#define PJD_MESH_LOADER_H

#include "raster.h"
#include "mesh.h"

int GenerateMeshTriangle(Mesh** mesh);
int GenerateMeshQuad(Mesh* mesh);
int LoadMeshFromFile(const char* filename, Mesh** outputMesh);

#endif // PJD_MESH_LOADER_H