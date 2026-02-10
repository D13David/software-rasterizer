#include "mesh_loader.h"
#include "texture_loader.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <math.h>

#define TINYOBJ_LOADER_C_IMPLEMENTATION
#include "tinyobj_loader.h"

static char* mmap_file(size_t* len, const char* filename) {
    HANDLE file =
        CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);

    if (file == INVALID_HANDLE_VALUE) { /* E.g. Model may not have materials. */
        return NULL;
    }

    HANDLE fileMapping = CreateFileMapping(file, NULL, PAGE_READONLY, 0, 0, NULL);
    assert(fileMapping != INVALID_HANDLE_VALUE);

    LPVOID fileMapView = MapViewOfFile(fileMapping, FILE_MAP_READ, 0, 0, 0);
    char* fileMapViewChar = (char*)fileMapView;
    assert(fileMapView != NULL);

    DWORD file_size = GetFileSize(file, NULL);
    (*len) = (size_t)file_size;

    return fileMapViewChar;
}

static void get_file_data(void* ctx, const char* filename, const int is_mtl, const char* obj_filename, char** data, size_t* len)
{
    (void)ctx;

    if (!filename) {
        (*data) = NULL;
        (*len) = 0;
        return;
    }

    size_t data_len = 0;

    *data = mmap_file(&data_len, filename);
    (*len) = data_len;
}

static void CalcNormal(float N[3], float v0[3], float v1[3], float v2[3]) {
    float v10[3];
    float v20[3];
    float len2;

    v10[0] = v1[0] - v0[0];
    v10[1] = v1[1] - v0[1];
    v10[2] = v1[2] - v0[2];

    v20[0] = v2[0] - v0[0];
    v20[1] = v2[1] - v0[1];
    v20[2] = v2[2] - v0[2];

    N[0] = v20[1] * v10[2] - v20[2] * v10[1];
    N[1] = v20[2] * v10[0] - v20[0] * v10[2];
    N[2] = v20[0] * v10[1] - v20[1] * v10[0];

    len2 = N[0] * N[0] + N[1] * N[1] + N[2] * N[2];
    if (len2 > 0.0f) {
        float len = (float)sqrt((double)len2);

        N[0] /= len;
        N[1] /= len;
    }
}

int GenerateMeshTriangle(Mesh* mesh)
{
    mesh->inputDesc[0].Type = InputElementType::TypePosition;
    mesh->inputDesc[0].Format = InputElementFormat::FormatRGB32F;
    mesh->inputDesc[0].Offset = 0;
    mesh->inputDesc[1].Type = InputElementType::TypeTexcoord;
    mesh->inputDesc[1].Format = InputElementFormat::FormatRG32F;
    mesh->inputDesc[1].Offset = mesh->inputDesc[1].Offset + 12; // color float3
    mesh->numInputElements = 2;

    int object_size = srInputStreamElementSize(mesh->inputDesc, mesh->numInputElements);
    size_t stride = object_size / sizeof(float);
    mesh->verts = (float*)malloc(object_size * 3);
    mesh->numTris = 3;

    float* vb = mesh->verts;

    vb[0 * stride + 0] =  0;
    vb[0 * stride + 1] =  1;
    vb[0 * stride + 2] =  0;
    vb[0 * stride + 3] = 0.5;
    vb[0 * stride + 4] = 0;

    vb[1 * stride + 0] =  1;
    vb[1 * stride + 1] = -1;
    vb[1 * stride + 2] =  0;
    vb[1 * stride + 3] = 1;
    vb[1 * stride + 4] = 1;

    vb[2 * stride + 0] = -1;
    vb[2 * stride + 1] = -1;
    vb[2 * stride + 2] =  0;
    vb[2 * stride + 3] = 0;
    vb[2 * stride + 4] = 1;

    return 1;
}

int GenerateMeshQuad(Mesh* mesh)
{
    mesh->inputDesc[0].Type = InputElementType::TypePosition;
    mesh->inputDesc[0].Format = InputElementFormat::FormatRGB32F;
    mesh->inputDesc[0].Offset = 0;

    mesh->inputDesc[1].Type = InputElementType::TypeTexcoord;
    mesh->inputDesc[1].Format = InputElementFormat::FormatRG32F;
    mesh->inputDesc[1].Offset = 12; // float3 position

    mesh->numInputElements = 2;

    int object_size = srInputStreamElementSize(mesh->inputDesc, mesh->numInputElements);
    size_t stride = object_size / sizeof(float);

    mesh->verts = (float*)malloc(object_size * 6);
    mesh->numTris = 2;

    float* vb = mesh->verts;

    vb[0 * stride + 0] = -1;
    vb[0 * stride + 1] = 1;
    vb[0 * stride + 2] = 0;
    vb[0 * stride + 3] = 0;
    vb[0 * stride + 4] = 0;

    vb[1 * stride + 0] = 1;
    vb[1 * stride + 1] = -1;
    vb[1 * stride + 2] = 0;
    vb[1 * stride + 3] = 1;
    vb[1 * stride + 4] = 1;

    vb[2 * stride + 0] = -1;
    vb[2 * stride + 1] = -1;
    vb[2 * stride + 2] = 0;
    vb[2 * stride + 3] = 0;
    vb[2 * stride + 4] = 1;

    vb[3 * stride + 0] = -1;
    vb[3 * stride + 1] = 1;
    vb[3 * stride + 2] = 0;
    vb[3 * stride + 3] = 0;
    vb[3 * stride + 4] = 0;

    vb[4 * stride + 0] = 1;
    vb[4 * stride + 1] = 1;
    vb[4 * stride + 2] = 0;
    vb[4 * stride + 3] = 1;
    vb[4 * stride + 4] = 0;

    vb[5 * stride + 0] = 1;
    vb[5 * stride + 1] = -1;
    vb[5 * stride + 2] = 0;
    vb[5 * stride + 3] = 1;
    vb[5 * stride + 4] = 1;

    return 1;
}

int LoadMeshFromFile(const char* filename, Mesh* mesh)
{
    tinyobj_attrib_t attrib;
    tinyobj_shape_t* shapes = NULL;
    size_t num_shapes;
    tinyobj_material_t* materials = NULL;
    size_t num_materials;

    unsigned int flags = TINYOBJ_FLAG_TRIANGULATE;
    int ret =
        tinyobj_parse_obj(&attrib, &shapes, &num_shapes, &materials,
            &num_materials, filename, get_file_data, NULL, flags);
    if (ret != TINYOBJ_SUCCESS) {
        return 0;
    }

    mesh->inputDesc[0].Type = InputElementType::TypePosition;
    mesh->inputDesc[0].Format = InputElementFormat::FormatRGB32F;
    mesh->inputDesc[0].Offset = 0;
    mesh->inputDesc[1].Type = InputElementType::TypeColor;
    mesh->inputDesc[1].Format = InputElementFormat::FormatRGB32F;
    mesh->inputDesc[1].Offset = mesh->inputDesc[0].Offset + 12; // pos float3
    mesh->inputDesc[2].Type = InputElementType::TypeTexcoord;
    mesh->inputDesc[2].Format = InputElementFormat::FormatRG32F;
    mesh->inputDesc[2].Offset = mesh->inputDesc[1].Offset + 12; // color float3
    mesh->numInputElements = 3;

    int num_triangles = attrib.num_face_num_verts;
    int object_size = srInputStreamElementSize(mesh->inputDesc, mesh->numInputElements);
    size_t stride = object_size / sizeof(float);
    mesh->verts = (float*)malloc(object_size * num_triangles * 3);
    mesh->numTris = num_triangles;

    if (num_materials >= 1)
    {
        mesh->texture = LoadTexture(materials[0].diffuse_texname);
    }

    size_t face_offset = 0;
    float* vb = mesh->verts;
    for (unsigned int i = 0; i < attrib.num_face_num_verts; i++) {
        size_t f;
        assert(attrib.face_num_verts[i] % 3 ==
            0); /* assume all triangle faces. */
        for (f = 0; f < (size_t)attrib.face_num_verts[i] / 3; f++) {
            size_t k;
            float v[3][3];
            float n[3][3];
            float t[3][2];
            float c[3];
            float len2;

            tinyobj_vertex_index_t idx0 = attrib.faces[face_offset + 3 * f + 0];
            tinyobj_vertex_index_t idx1 = attrib.faces[face_offset + 3 * f + 1];
            tinyobj_vertex_index_t idx2 = attrib.faces[face_offset + 3 * f + 2];

            for (k = 0; k < 3; k++) {
                int f0 = idx0.v_idx;
                int f1 = idx1.v_idx;
                int f2 = idx2.v_idx;
                assert(f0 >= 0);
                assert(f1 >= 0);
                assert(f2 >= 0);

                v[0][k] = attrib.vertices[3 * (size_t)f0 + k];
                v[1][k] = attrib.vertices[3 * (size_t)f1 + k];
                v[2][k] = attrib.vertices[3 * (size_t)f2 + k];
            }

            if (attrib.num_normals > 0) {
                int f0 = idx0.vn_idx;
                int f1 = idx1.vn_idx;
                int f2 = idx2.vn_idx;
                if (f0 >= 0 && f1 >= 0 && f2 >= 0) {
                    assert(f0 < (int)attrib.num_normals);
                    assert(f1 < (int)attrib.num_normals);
                    assert(f2 < (int)attrib.num_normals);
                    for (k = 0; k < 3; k++) {
                        n[0][k] = attrib.normals[3 * (size_t)f0 + k];
                        n[1][k] = attrib.normals[3 * (size_t)f1 + k];
                        n[2][k] = attrib.normals[3 * (size_t)f2 + k];
                    }
                }
                else { /* normal index is not defined for this face */
                    /* compute geometric normal */
                    CalcNormal(n[0], v[0], v[1], v[2]);
                    n[1][0] = n[0][0];
                    n[1][1] = n[0][1];
                    n[1][2] = n[0][2];
                    n[2][0] = n[0][0];
                    n[2][1] = n[0][1];
                    n[2][2] = n[0][2];
                }
            }
            else {
                /* compute geometric normal */
                CalcNormal(n[0], v[0], v[1], v[2]);
                n[1][0] = n[0][0];
                n[1][1] = n[0][1];
                n[1][2] = n[0][2];
                n[2][0] = n[0][0];
                n[2][1] = n[0][1];
                n[2][2] = n[0][2];
            }

            if (attrib.num_texcoords > 0) {
                int f0 = idx0.vt_idx;
                int f1 = idx1.vt_idx;
                int f2 = idx2.vt_idx;
                if (f0 >= 0 && f1 >= 0 && f2 >= 0) {
                    assert(f0 < (int)attrib.num_texcoords);
                    assert(f1 < (int)attrib.num_texcoords);
                    assert(f2 < (int)attrib.num_texcoords);
                    for (k = 0; k < 2; k++) {
                        t[0][k] = attrib.texcoords[2 * (size_t)f0 + k];
                        t[1][k] = attrib.texcoords[2 * (size_t)f1 + k];
                        t[2][k] = attrib.texcoords[2 * (size_t)f2 + k];
                    }
                }
                else
                {
                }
            }

            for (k = 0; k < 3; k++) {
                vb[(3 * i + k) * stride + 0] = v[k][0];
                vb[(3 * i + k) * stride + 1] = v[k][1];
                vb[(3 * i + k) * stride + 2] = v[k][2];

                /* Set the normal as alternate color */
                c[0] = n[k][0];
                c[1] = n[k][1];
                c[2] = n[k][2];
                len2 = c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
                if (len2 > 0.0f) {
                    float len = (float)sqrt((double)len2);
                    c[0] /= len;
                    c[1] /= len;
                    c[2] /= len;
                }

                vb[(3 * i + k) * stride + 3] = (c[0] * 0.5f + 0.5f);
                vb[(3 * i + k) * stride + 4] = (c[1] * 0.5f + 0.5f);
                vb[(3 * i + k) * stride + 5] = (c[2] * 0.5f + 0.5f);

                /* Set the vertex color */
                vb[(3 * i + k) * stride + 6] = t[k][0];
                vb[(3 * i + k) * stride + 7] = t[k][1];

            }
        }
        /* You can access per-face material through attrib.material_ids[i] */

        face_offset += (size_t)attrib.face_num_verts[i];
    }

    tinyobj_attrib_free(&attrib);
    tinyobj_shapes_free(shapes, num_shapes);
    tinyobj_materials_free(materials, num_materials);

    return 1;
}