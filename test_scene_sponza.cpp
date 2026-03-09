#include "shared.h"
#include "mesh_loader.h"
#include "texture_loader.h"
#include "thirteen.h"

#define FAST_OBJ_IMPLEMENTATION
#include "fast_obj.h"

#include <vector>

#if PJD_ACTIVE_SCENE == TEST_SCENE_SPONZA

struct Vertex {
    vec3 pos;
    vec3 normal;
    vec2 uv;
};

struct SubMesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    int primitives = 0;
    TextureView texture;
};

std::vector<SubMesh> submeshes;
vec3 PlayerPos;
float Yaw = DEG2RAD(90.0f);
float Pitch;
mat4 WorldViewProj;
int centerX, centerY;

float MouseSensitivity = 0.0025f;
float MoveSpeed = 200.0f;

vec3 SunLightDir;

void aces_filmic(float* color) 
{
    for (int c = 0; c < 3; ++c) 
    {
        float x = color[c] * 0.6f;
        color[c] = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
        color[c] = Clamp(color[c], 0, 1);
    }
}

static rgba8 ShadePixel(float mipLevel, const Interpolants* interp)
{
    color4 texColor;

    ConvertRGBA8(SampleTextureLod(interp->px, interp->py, interp->u, interp->v, mipLevel), texColor);

    vec3 normal{ interp->nx, interp->ny, interp->nz };

    float diffuse = max(Vec3Dot(SunLightDir, normal), 0.1f) * 2.5;

    color4 litColor;
    Color4Mul(texColor, diffuse, litColor);

    aces_filmic(litColor);

    return ConvertColor4(litColor);
}

bool SceneInitialize()
{
    fastObjMesh* mesh = fast_obj_read("./meshes/sponza.obj");
    if (!mesh) {
        return false;
    }

    submeshes.resize(mesh->material_count);

    uint32_t indexOffset = 0;

    for (uint32_t face = 0; face < mesh->face_count; face++)
    {
        uint32_t material = mesh->face_materials[face];
        SubMesh& sub = submeshes[material];
        
        uint32_t fv = mesh->face_vertices[face];

        for (uint32_t v = 0; v < fv; v++)
        {
            fastObjIndex gi = mesh->indices[indexOffset + v];

            Vertex vert{};

            vert.pos[0] = mesh->positions[3 * gi.p + 0];
            vert.pos[1] = mesh->positions[3 * gi.p + 1];
            vert.pos[2] = mesh->positions[3 * gi.p + 2];

            if (gi.n)
            {
                
                vert.normal[0] = mesh->normals[3 * gi.n + 0];
                vert.normal[1] = mesh->normals[3 * gi.n + 1];
                vert.normal[2] = mesh->normals[3 * gi.n + 2];
            }

            if (gi.t)
            {
                vert.uv[0] = mesh->texcoords[2 * gi.t + 0];
                vert.uv[1] = mesh->texcoords[2 * gi.t + 1];
            }

            sub.vertices.push_back(vert);
            sub.indices.push_back((uint32_t)sub.vertices.size() - 1);
        }

        assert(fv == 3);

        indexOffset += fv;

        sub.primitives++;
    }

    for (int i = 0; i < mesh->material_count; ++i)
    {
        SubMesh& sub = submeshes[i];
        sub.texture = LoadTexture(mesh->textures[mesh->materials[i].map_Kd].path);
        if (sub.texture.Data == NULL) {
            sub.texture = LoadCheckerboardTexture();
        }
    }

    fast_obj_destroy(mesh);

    ShowCursor(FALSE);

    centerX = GetSystemMetrics(SM_CXSCREEN) / 2;
    centerY = GetSystemMetrics(SM_CYSCREEN) / 2;

    RECT screenRect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    ClipCursor(&screenRect);

    SetCursorPos(centerX, centerY);

    return true;
}

void SceneDestroy()
{
    ClipCursor(nullptr);
    ShowCursor(TRUE);
}

static void MouseDelta(vec2 out)
{
    POINT p;
    if (GetCursorPos(&p))
    {
        out[0] = centerX - p.x;
        out[1] = centerY - p.y;

        SetCursorPos(centerX, centerY);
    }
    else
    {
        out[0] = 0;
        out[1] = 0;
    }
}

static void HandleInput()
{
    vec2 mouseDelta;
    vec3 tmp;
    MouseDelta(mouseDelta);

#if 1
    Yaw   -= mouseDelta[0] * MouseSensitivity;
    Pitch += mouseDelta[1] * MouseSensitivity;
    Pitch  = Clamp(Pitch, -1.5f, 1.5f);
#else
    PlayerPos[1] = 100;
#endif

    vec3 forward = { cosf(Pitch) * sinf(Yaw),
                     sinf(Pitch),
                     cosf(Pitch) * cosf(Yaw)
                   };

    
    vec3 right;
    vec3 up = { 0, 1, 0 };

    Vec3Cross(up, forward, right);
    Vec3NormalizeSelf(right);
    Vec3Cross(forward, right, up);

    float speed = MoveSpeed * DeltaTime;

    if (Thirteen::GetKey('W'))
    {
        Vec3Mul(forward, speed, tmp);
        Vec3Add(PlayerPos, tmp, PlayerPos);
    }

    if (Thirteen::GetKey('S'))
    {
        Vec3Mul(forward, speed, tmp);
        Vec3Sub(PlayerPos, tmp, PlayerPos);
    }

    if (Thirteen::GetKey('D'))
    {
        Vec3Mul(right, speed, tmp);
        Vec3Add(PlayerPos, tmp, PlayerPos);
    }

    if (Thirteen::GetKey('A'))
    {
        Vec3Mul(right, speed, tmp);
        Vec3Sub(PlayerPos, tmp, PlayerPos);
    }

    vec3 target;
    Vec3Add(PlayerPos, forward, target);

    mat4 ViewMatrix;
    CreateMatrixLookAt(PlayerPos, target, up, ViewMatrix);

    mat4 WorldView;
    //Matrix4Mul(WorldMatrix, ViewMatrix, WorldView);
    CopyMatrix(ViewMatrix, WorldView);

    mat4 ProjectionMatrix;
    CreateMatrixPerspectiveFovLH(60.0f, FB_WIDTH / (float)FB_HEIGHT, 10, 3000.0f, ProjectionMatrix);

    Matrix4Mul(WorldView, ProjectionMatrix, WorldViewProj);
}

void SceneRenderFrame()
{
    HandleInput();

#if 0
    static float timeOfDay = 0.0f;
    timeOfDay += 0.06f * DeltaTime;
    if (timeOfDay > 1.0f)
        timeOfDay -= 1.0f;
    float sunAngle = timeOfDay * 2.0f * 3.14159265f; // full rotation in radians

    // Assuming sun moves along X-Z plane (horizon rotation)
    SunLightDir[0] = cos(sunAngle);
    SunLightDir[1] = sin(sunAngle); // vertical movement
    SunLightDir[2] = 0.0f;
#else
    SunLightDir[0] = -0.4f;
    SunLightDir[1] =  0.9f;
    SunLightDir[2] = 0.0f;
#endif

    Clear(RGB(0, 0, 0));

    SetTextureFilter(TextureFilter::Unreal);
    SetDrawMode(DrawMode::Solid);
    SetPixelShader(ShadePixel);

    InputElementDescriptor VertexLayout[] = {
        { InputElementType::Position, 0,  InputElementFormat::FLOAT3, 0, offsetof(Vertex, pos) },
        { InputElementType::Texcoord, 0,  InputElementFormat::FLOAT2, 0, offsetof(Vertex, uv) },
        { InputElementType::Normal,   0,  InputElementFormat::FLOAT3, 0, offsetof(Vertex, normal) },
    };
    uint32_t VertexLayoutCount = sizeof(VertexLayout) / sizeof(VertexLayout[0]);

    for (int i = 0; i < submeshes.size(); ++i)
    {
        SubMesh& submesh = submeshes[i];
        SetTextureView(submesh.texture);

        DrawTriangleList
        (
            submesh.vertices.data(),
            submesh.indices.data(),
            VertexLayout, 
            VertexLayoutCount, 
            submesh.primitives, 
            WorldViewProj,
            true
        );
    }
}

void SceneRenderOverlay2D()
{
    //WriteString(Format("Player Pos: %f, %f, %f", PlayerPos[0], PlayerPos[1], PlayerPos[2]), 10, 500);
    //WriteString(Format("Light Dir: %f, %f, %f", SunLightDir[0], SunLightDir[1], SunLightDir[2]), 10, 500);
}

#endif // TEST_SCENE_EMPTY