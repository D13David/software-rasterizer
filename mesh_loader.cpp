#include "mesh_loader.h"
#include "texture_loader.h"
#include "mesh.h"

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

int GenerateMeshTriangle(Mesh** outputMesh)
{
    const int vertexSize = sizeof(vec3) + sizeof(vec2);
    Mesh* mesh = MeshCreate(vertexSize, 3, 1);
    assert(mesh != NULL);
    assert(mesh->Surfaces != NULL);

    *outputMesh = mesh;

    mesh->InputDesc[0].Type = InputElementType::Position;
    mesh->InputDesc[0].Format = InputElementFormat::FLOAT3;
    mesh->InputDesc[0].Offset = 0;
    mesh->InputDesc[1].Type = InputElementType::Texcoord;
    mesh->InputDesc[1].Format = InputElementFormat::FLOAT2;
    mesh->InputDesc[1].Offset = mesh->InputDesc[0].Offset + 12; // color float3
    mesh->NumInputElements = 2;

    size_t stride = vertexSize / sizeof(float);
    float* vb = (float*)mesh->VertexBuffer;

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

    uint16_t* indices = (uint16_t*)malloc(3 * sizeof(uint16_t));
    assert(indices != NULL);
    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;

    mesh->Surfaces[0].IndexBuffer = indices;
    mesh->Surfaces[0].Texture = LoadCheckerboardTexture();
    mesh->Surfaces[0].NumPrimitives = 1;

    return 1;
}

int GenerateMeshQuad(Mesh** outputMesh, float width, float height)
{
    const int vertexSize = sizeof(vec3) + sizeof(vec2);
    Mesh* mesh = MeshCreate(vertexSize, 4, 1);
    assert(mesh != NULL);
    assert(mesh->Surfaces != NULL);

    *outputMesh = mesh;

    mesh->InputDesc[0].Type = InputElementType::Position;
    mesh->InputDesc[0].Format = InputElementFormat::FLOAT3;
    mesh->InputDesc[0].Offset = 0;

    mesh->InputDesc[1].Type = InputElementType::Texcoord;
    mesh->InputDesc[1].Format = InputElementFormat::FLOAT2;
    mesh->InputDesc[1].Offset = 12;

    mesh->NumInputElements = 2;

    size_t stride = vertexSize / sizeof(float);
    float* vb = (float*)mesh->VertexBuffer;

    float hw = width * 0.5f;
    float hh = height * 0.5f;

    // --- Aspect-preserving UV scale ---
    float uScale = 1.0f;
    float vScale = 1.0f;

    if (width > height)
        uScale = width / height;
    else
        vScale = height / width;

    // Top-left
    vb[0 * stride + 0] = -hw;
    vb[0 * stride + 1] = hh;
    vb[0 * stride + 2] = 0.0f;
    vb[0 * stride + 3] = 0.0f;
    vb[0 * stride + 4] = 0.0f;

    // Top-right
    vb[1 * stride + 0] = hw;
    vb[1 * stride + 1] = hh;
    vb[1 * stride + 2] = 0.0f;
    vb[1 * stride + 3] = uScale;
    vb[1 * stride + 4] = 0.0f;

    // Bottom-right
    vb[2 * stride + 0] = hw;
    vb[2 * stride + 1] = -hh;
    vb[2 * stride + 2] = 0.0f;
    vb[2 * stride + 3] = uScale;
    vb[2 * stride + 4] = vScale;

    // Bottom-left
    vb[3 * stride + 0] = -hw;
    vb[3 * stride + 1] = -hh;
    vb[3 * stride + 2] = 0.0f;
    vb[3 * stride + 3] = 0.0f;
    vb[3 * stride + 4] = vScale;

    uint16_t* indices = (uint16_t*)malloc(6 * sizeof(uint16_t));
    indices[0] = 0; indices[1] = 1; indices[2] = 2;
    indices[3] = 0; indices[4] = 2; indices[5] = 3;

    mesh->Surfaces[0].IndexBuffer = indices;
    mesh->Surfaces[0].Texture = LoadCheckerboardTexture();
    mesh->Surfaces[0].NumPrimitives = 2;

    return 1;
}

int LoadMeshFromFile(const char* filename, Mesh** outputMesh)
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

    const int vertexSize = sizeof(vec3) + sizeof(vec2);
    Mesh* mesh = MeshCreate(vertexSize, 
                            attrib.num_vertices,
                            1);
    assert(mesh != NULL);
    assert(mesh->Surfaces != NULL);

    *outputMesh = mesh;

    mesh->InputDesc[0].Type = InputElementType::Position;
    mesh->InputDesc[0].Format = InputElementFormat::FLOAT3;
    mesh->InputDesc[0].Offset = 0;
    mesh->InputDesc[1].Type = InputElementType::Texcoord;
    mesh->InputDesc[1].Format = InputElementFormat::FLOAT2;
    mesh->InputDesc[1].Offset = mesh->InputDesc[0].Offset + 12; // color float3
    mesh->NumInputElements = 2;

    mesh->Surfaces[0].IndexBuffer = (uint16_t*)malloc(attrib.num_face_num_verts * 3 * sizeof(uint16_t));
    mesh->Surfaces[0].NumPrimitives = attrib.num_face_num_verts;

    if (num_materials >= 1) {
        mesh->Surfaces[0].Texture = LoadTexture(materials[0].diffuse_texname);
    }

    size_t face_offset = 0;
    uint8_t* vertexBufferStart = (uint8_t*)mesh->VertexBuffer;
    for (int i = 0; i < attrib.num_face_num_verts; ++i)
    {
        assert(attrib.face_num_verts[i] % 3 == 0);

        for (int j = 0; j < attrib.face_num_verts[i]; ++j)
        {
            tinyobj_vertex_index_t index = attrib.faces[face_offset + j];

            mesh->Surfaces[0].IndexBuffer[face_offset + j] = index.v_idx;

            float* vertex = (float*)&vertexBufferStart[index.v_idx * vertexSize];

            // pos
            *vertex++ = attrib.vertices[3 * index.v_idx + 0];
            *vertex++ = attrib.vertices[3 * index.v_idx + 1];
            *vertex++ = attrib.vertices[3 * index.v_idx + 2];

            // FIXME: shared uv issue
            // uv
            if (attrib.num_texcoords != 0)
            {
                *vertex++ = attrib.texcoords[2 * index.vt_idx + 0];
                *vertex++ = attrib.texcoords[2 * index.vt_idx + 1];
            }
            else
            {
                *vertex++ = 0.0f;
                *vertex++ = 0.0f;
            }
        }

        face_offset += attrib.face_num_verts[i];
    }

    tinyobj_attrib_free(&attrib);
    tinyobj_shapes_free(shapes, num_shapes);
    tinyobj_materials_free(materials, num_materials);

    return 1;
}