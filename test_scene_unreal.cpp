#include "profile.h"
#include "raster.h"
#include "shared.h"
#include "mesh.h"
#include "uarch_loader.h"
#include "texture_loader.h"

#if PJD_ACTIVE_SCENE == TEST_SCENE_UNREAL

Mesh* Gmesh[10];
int numMeshes;

bool SceneInitialize()
{
    auto archive = OpenArchive("../Unreal/System/UnrealI.u");
    if (archive)
    {
        //DumpExportTable(archive, "Mesh");
        struct LoadInfo
        {
            const char* name;
            const char* skin;
        };
        LoadInfo loadInfos[] = {
            { "Skaarjw", "./meshes/textures/Skaarjw3.tga" },
            { "Merc", "./meshes/textures/JMerc1.tga" },
            { "Male2", "./meshes/textures/Ash.tga" },
            { "Female2", "./meshes/textures/F2Female2.tga" },
            { "Female1", "./meshes/textures/gina.tga" },
            { "Brute1", "./meshes/textures/jBrute1.tga" },
            { "GasBagM", "./meshes/textures/GasBag2.tga" },
            { NULL, NULL }
        };
        for (numMeshes = 0; loadInfos[numMeshes].name; ++numMeshes) {
            if (LoadMeshFromArchive(archive, loadInfos[numMeshes].name, &Gmesh[numMeshes])) {
                Gmesh[numMeshes]->Surfaces[0].Texture = LoadTexture(loadInfos[numMeshes].skin);
            }
        }
        CloseArchive(archive);
    }

    if (!Gmesh) {
        return false;
    }

    return true;
}

void SceneDestroy()
{
    for (int i = 0; i < numMeshes; ++i) {
        MeshFree(Gmesh[i]);
    }
}

static void DrawMesh(Mesh* mesh, float tx, float ty, float tz)
{
    PROFILE_AUTO("DrawMesh");

    // object transform
    mat4 ObjectTransform;
    CreateMatrixTransform(tx, ty, tz, ObjectTransform);

    static float angle = 180;
    angle += 10 * DeltaTime;
    if (angle > 360.0f) angle -= 360.0f;

    /*mat4_t ObjectRotateX;
    CreateMatrixRotateX(DEG2RAD(-20.0f), ObjectRotateX);*/

    mat4 ObjectRotateY;
    CreateMatrixRotateY(DEG2RAD(angle), ObjectRotateY);

    mat4 WorldMat;
    //MatrixMultiply(ObjectRotateX, ObjectRotateY, WorldMat);
    Matrix4Mul(ObjectRotateY, ObjectTransform, WorldMat);

    //// projection
    mat4 ProjectionMat;
    CreateMatrixPerspectiveFovLH(60.0f, FB_WIDTH / (float)FB_HEIGHT, 0.1f, 2000.0f, ProjectionMat);

    // world view projection
    mat4 WorldViewProj;
    Matrix4Mul(WorldMat, ProjectionMat, WorldViewProj);

    for (int i = 0; i < mesh->NumSurfaces; ++i)
    {
        const Surface* surface = &mesh->Surfaces[i];

        SetTextureView(surface->Texture);

        DrawTriangleList
        (
            mesh->NumAnimSeqs == 0 ? mesh->VertexBuffer : mesh->FrameCache,
            surface->IndexBuffer,
            mesh->InputDesc,
            mesh->NumInputElements,
            surface->NumPrimitives,
            WorldViewProj,
            true
        );

        if (WireFrameOverlay)
        {
            SetDrawMode(DrawMode::Wireframe);

            DrawTriangleList
            (
                mesh->NumAnimSeqs == 0 ? mesh->VertexBuffer : mesh->FrameCache,
                surface->IndexBuffer,
                mesh->InputDesc,
                mesh->NumInputElements,
                surface->NumPrimitives,
                WorldViewProj,
                true
            );

            SetDrawMode(DrawMode::Solid);
        }
    }
}

void SceneRenderFrame()
{
    PROFILE_AUTO("Frame");

    {
        PROFILE_AUTO("Update Animations");

        static float frame = 0;
        frame += DeltaTime * 10;

        for (int i = 0; i < numMeshes; ++i)
        {
            auto anim = FindAnimSequence(Gmesh[i], "Walk");
            if (!anim) {
                anim = FindAnimSequence(Gmesh[i], "Float");
            }
            UpdateFrame(Gmesh[i], anim, frame);
        }
    }

    Clear(RGB(0, 0, 0));

    SetTextureFilter(TextureFilter::Unreal);

    SetDrawMode(DrawMode::Solid);
    for (int i = 0; i < numMeshes; ++i) {
        DrawMesh(Gmesh[i], (i - numMeshes / 2) * 250, 0, 400);
    }
}

void SceneRenderOverlay2D()
{
}

#endif // TEST_SCENE_UNREAL