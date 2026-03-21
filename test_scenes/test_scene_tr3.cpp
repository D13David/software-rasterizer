#include "shared.h"
#include "common/common.h"
#include "renderer/texture_loader.h"
#include "renderer/raster.h"
#include "external/thirteen.h"
#include <unordered_map>

// Reference https://opentomb.github.io/TRosettaStone3/trosettastone.html#level_format_tr3

#if PJD_ACTIVE_SCENE == TEST_SCENE_TR3

#define MAX_TEXTURE_PAGES   32
#define MAX_TEXTURES        1024*3
#define MAX_STATIC_OBJECTS  64
#define MAX_BATCH_INDICES   60000
#define MAX_BATCH_VERTICES  60000

#define FIXED_8_8(value) (((value) >> 8) + (value & 0xff) / 256.0f) 
#define TO_FIXED_8_8(value) ((int16_t)((value) * 256.0f))

// check https://opentomb.github.io/TRosettaStone3/trosettastone.html#_entity_types
enum Tr3Entities
{
    LARA                = 0,

    DOG                 = 22,
    RAT                 = 23,
    KILL_ALL_TRIGGERS   = 24,
    KILLER_WHALE        = 25,
    SCUBA_DIVER         = 26,
    CROW                = 27,
    TIGER               = 28,
    VULTURE             = 29,

    SKYBOX              = 355,

    NUM_ENTITIES        = 374
};

struct Tr3RoomStaticMesh
{
    int32_t x, y, z;
    uint16_t Rotation;
    uint16_t Colour;
    uint16_t Unused;
    uint16_t MeshID;
};

typedef struct Tr3Room
{
    int32_t x, y, z;
    int16_t* Data;
    Tr3RoomStaticMesh* Meshes;
    uint16_t NumMeshes;
} Tr3Room;

typedef struct Tr3QuadFace
{
    uint16_t Vertices[4];
    uint16_t Texture;
} Tr3QuadFace;

typedef struct Tr3TriFace
{
    uint16_t Vertices[3];
    uint16_t Texture;
} Tr3TriFace;

typedef struct Tr3Texture
{
    uint16_t Attribute; /* 1 = alpha tested, 2 = additive blending */
    uint16_t Page;
    uint16_t uv[4][2];
} Tr3Texture;

typedef struct Tr3Entity
{
    uint16_t NumMeshes;
    uint16_t MeshOffset;
    uint32_t BoneOffset;
    uint32_t FrameOffset;
    uint16_t Animation;
} Tr3Entity;

typedef struct Tr3StaticObject
{
    uint16_t Mesh;
    int16_t  VisibilityBox[6];
    int16_t  CollisionBox[6];
    uint16_t Flags;
} Tr3StaticObject;

typedef struct Vertex
{
    float x, y, z;          // 12
    float u, v;             //  8
    float r, g, b;          // 12
} Vertex;

typedef struct Batch
{
    Vertex      Vertices[MAX_BATCH_VERTICES];
    uint32_t    Indices[MAX_BATCH_INDICES];
    int         NumVertices;
    int         NumIndices;
} Batch;

typedef struct DrawInfo
{
    const int16_t*  VertexBuffer;
    Tr3QuadFace*    Quads;
    int             NumQuads;
    Tr3TriFace*     Tris;
    int             NumTris;
    Tr3QuadFace*    ColoredQuads;
    int             NumColoredQuads;
    Tr3TriFace*     ColoredTris;
    int             NumColoredTris;
    uint8_t         Stride;
} DrawInfo;

#define TRIANGLE_BUCKETS (MAX_TEXTURE_PAGES+1)

// tomb raider 3 level data
static uint8_t         Palette16[256 * 4];
static TextureView     TexturePage[MAX_TEXTURE_PAGES + 1];
static uint16_t        NumRooms;
static Tr3Room*        Rooms;
static Tr3Texture      Textures[MAX_TEXTURES];
static uint32_t        NumTextures;
static int16_t*        MeshData;
static int16_t**       Meshes;
static uint32_t        NumEntities;
static uint32_t        NumStaticObjects;
static Tr3Entity       Entities[NUM_ENTITIES];
static Tr3StaticObject StaticObjects[MAX_STATIC_OBJECTS];

static bool         BatchUsed[TRIANGLE_BUCKETS] = {};
static Batch        PrimitiveBatches[TRIANGLE_BUCKETS];

static vec3         PlayerPos;
static float        Yaw = DEG2RAD(90.0f);
static float        Pitch;
static mat4         ViewMatrix;
static mat4         ProjectionMatrix;
static int          centerX;
static int          centerY;

static bool         LightmapsOnly;

const float         MouseSensitivity = 0.0025f;
const float         MoveSpeed = 4.0f;
const float         WorldScale = 200.0f;

InputElementDescriptor VertexLayout[] = {
        { InputElementType::Position, 0,  InputElementFormat::FLOAT3, 0,  offsetof(Vertex, x) },
        { InputElementType::Texcoord, 0,  InputElementFormat::FLOAT2, 0,  offsetof(Vertex, u) },
        { InputElementType::Color,    0,  InputElementFormat::FLOAT3, 0,  offsetof(Vertex, r) }
};
const uint32_t VertexLayoutCount = sizeof(VertexLayout) / sizeof(VertexLayout[0]);

template<typename T>
static T Read(FILE* fp)
{
    T result;
    assert(fp != NULL);
    CHECKED(fread(&result, sizeof(T), 1, fp) == 1);
    return result;
}

static void Skip(FILE* fp, int elementSize, int numElements)
{
    fseek(fp, elementSize * numElements, SEEK_CUR);
}

template<typename T>
static void ReadCountAndSkip(FILE* fp, int elementSize)
{
    T numElements = Read<T>(fp);
    if (numElements > 0) {
        Skip(fp, elementSize, numElements);
    }
}

static void LoadPalettes(FILE* fp)
{
    // 8 bit palette
    Skip(fp, 3, 256);

    // 16 bit palette
    fread(&Palette16, 4, 256, fp);
}

static void LoadTextureAtlases(FILE* fp)
{
    uint32_t NumTexturePages = Read<uint32_t>(fp);
    fseek(fp, 65536 * NumTexturePages, SEEK_CUR);

    assert(NumTexturePages < MAX_TEXTURE_PAGES);

    uint16_t page16bit[0x10000];
    for (uint32_t i = 0; i < NumTexturePages; ++i)
    {
        fread(page16bit, sizeof(uint16_t), sizeof(page16bit) / sizeof(uint16_t), fp);

        rgba8 buffer[0x10000], *dst = buffer;
        for (int j = 0; j < 256 * 256; ++j)
        {
            *dst++ = RGBA_PACK(
                ((page16bit[j] & 0x7c00) >> 10) << 3,
                ((page16bit[j] & 0x03e0) >>  5) << 3,
                ((page16bit[j] & 0x001f)      ) << 3,
                ((page16bit[j] & 0x8000) <<  8)
            );
        }

        TexturePage[i] = LoadTextureFromMemory(buffer, 256, 256);
    }

    TexturePage[MAX_TEXTURE_PAGES] = LoadColorTexture(RGBA(255, 255, 255, 255));
}

static void LoadRooms(FILE* fp)
{
    NumRooms = Read<uint16_t>(fp);

    Rooms = (Tr3Room*)malloc(sizeof(Tr3Room) * NumRooms);

    for (int i = 0; i < NumRooms; ++i)
    {
        Rooms[i].x = Read<int32_t>(fp);
        Rooms[i].y = 0;
        Rooms[i].z = Read<int32_t>(fp);
        
        // room min/max extend
        Read<int32_t>(fp);
        Read<int32_t>(fp);

        uint32_t size = Read<uint32_t>(fp);
        Rooms[i].Data = (int16_t*)malloc(sizeof(int16_t) * size);
        fread(Rooms[i].Data, sizeof(int16_t), size, fp);

        // portals
        ReadCountAndSkip<uint16_t>(fp, 32);

        // sector data
        uint16_t NumZSectors = Read<uint16_t>(fp);
        uint16_t NumXSectors = Read<uint16_t>(fp);
        Skip(fp, 8, NumZSectors * NumXSectors);

        // ambient light
        Read<int16_t>(fp);
        // light mode
        Read<int16_t>(fp);

        // room lights
        ReadCountAndSkip<uint16_t>(fp, 24);

        // static meshes
        Rooms[i].NumMeshes = Read<uint16_t>(fp);
        if (Rooms[i].NumMeshes)
        {
            Rooms[i].Meshes = (Tr3RoomStaticMesh*)malloc(Rooms[i].NumMeshes * sizeof(Tr3RoomStaticMesh));
            fread(Rooms[i].Meshes, sizeof(Tr3RoomStaticMesh), Rooms[i].NumMeshes, fp);
        }
        else
        {
            Rooms[i].Meshes = NULL;
        }
        //ReadCountAndSkip<uint16_t>(fp, 20);

        Read<int16_t>(fp); // alternate room
        Read<int16_t>(fp); // flags
        Read<uint8_t>(fp); // water scheme
        Read<uint8_t>(fp); // reverb info
        Read<uint8_t>(fp); // padding
    }

    // floor data
    ReadCountAndSkip<uint32_t>(fp, 2);
}

static void LoadObjects(FILE* fp)
{
    uint32_t sizeMeshData = Read<uint32_t>(fp);
    MeshData = (int16_t*)malloc(sizeMeshData * sizeof(int16_t));
    fread(MeshData, sizeMeshData, sizeof(int16_t), fp);

    uint32_t numMeshes = Read<uint32_t>(fp);
    Meshes = (int16_t**)malloc(numMeshes * sizeof(int16_t*));
    for (int i = 0; i < numMeshes; ++i)
    {
        uint32_t offset;
        fread(&offset, sizeof(uint32_t), 1, fp);
        Meshes[i] = MeshData + (offset / 2);
    }

    // animations
    ReadCountAndSkip<uint32_t>(fp, 32);

    // state changes
    ReadCountAndSkip<uint32_t>(fp, 6);

    // animation dispatch
    ReadCountAndSkip<uint32_t>(fp, 8);

    // animation commands
    ReadCountAndSkip<uint32_t>(fp, 2);

    // mesh tree
    ReadCountAndSkip<uint32_t>(fp, 4);

    // frames
    ReadCountAndSkip<uint32_t>(fp, 2);

    NumEntities = Read<uint32_t>(fp);
    for (int i = 0; i < NumEntities; ++i)
    {
        uint32_t id = Read<uint32_t>(fp);
        Entities[id].NumMeshes = Read<uint16_t>(fp);
        Entities[id].MeshOffset = Read<uint16_t>(fp);
        Entities[id].BoneOffset = Read<uint32_t>(fp);
        Entities[id].FrameOffset = Read<uint32_t>(fp);
        Entities[id].Animation = Read<uint16_t>(fp);
    }

    NumStaticObjects = Read<uint32_t>(fp);
    for (int i = 0; i < NumStaticObjects; ++i)
    {
        uint32_t id = Read<uint32_t>(fp);
        assert(id < MAX_STATIC_OBJECTS);
        StaticObjects[id].Mesh = Read<uint16_t>(fp);
        fread(StaticObjects[id].VisibilityBox, sizeof(int16_t), 6, fp);
        fread(StaticObjects[id].CollisionBox, sizeof(int16_t), 6, fp);
        StaticObjects[id].Flags = Read<uint16_t>(fp);
    }
}

static void LoadSprites(FILE* fp)
{
    // sprite textures
    ReadCountAndSkip<uint32_t>(fp, 16);

    // sprite sequences
    ReadCountAndSkip<uint32_t>(fp, 8);
}

static void LoadCameras(FILE* fp)
{
    // cameras
    ReadCountAndSkip<uint32_t>(fp, 16);
}

static void LoadSoundEffects(FILE* fp)
{
    // sound sources
    ReadCountAndSkip<uint32_t>(fp, 16);
}

static void LoadBoxes(FILE* fp)
{
    // boxes
    uint32_t numBoxes = Read<uint32_t>(fp);
    Skip(fp, 8, numBoxes);

    // overlaps
    ReadCountAndSkip<uint32_t>(fp, 2);

    // zones
    Skip(fp, 20, numBoxes);
}

static void LoadTextures(FILE* fp)
{
    // animated textures
    ReadCountAndSkip<uint32_t>(fp, 2);

    NumTextures = Read<uint32_t>(fp);
    assert(NumTextures < MAX_TEXTURES);
    fread(Textures, sizeof(Tr3Texture), NumTextures, fp);
}

static bool LoadTombRaider3Level(const char* path)
{
    FILE* fp = NULL;
    if (fopen_s(&fp, path, "rb") != NULL) {
        return false;
    }

    uint32_t version = Read<uint32_t>(fp);

    LoadPalettes(fp);
    LoadTextureAtlases(fp);
    Read<uint32_t>(fp);
    LoadRooms(fp);
    LoadObjects(fp);
    LoadSprites(fp);
    LoadCameras(fp);
    LoadSoundEffects(fp);
    LoadBoxes(fp);
    LoadTextures(fp);
    
    // don't care of the rest

    fclose(fp);

    return true;
}

bool SceneInitialize()
{
    if (!LoadTombRaider3Level("C:\\Program Files (x86)\\Steam\\steamapps\\common\\TombRaider (III)\\data\\JUNGLE.TR2")) {
        return false;
    }

    ShowCursor(FALSE);

    centerX = GetSystemMetrics(SM_CXSCREEN) / 2;
    centerY = GetSystemMetrics(SM_CYSCREEN) / 2;

    RECT screenRect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    ClipCursor(&screenRect);

    SetCursorPos(centerX, centerY);

    PlayerPos[0] = 157.059265;
    PlayerPos[1] = -129.910492;
    PlayerPos[2] = 384.315460;
    Yaw = 3.315797;
    Pitch = 0.007500;

    return true;
}

void SceneDestroy()
{
    free(Meshes);
    free(MeshData);
    for (int i = 0; i < NumRooms; ++i) {
        free(Rooms[i].Data);
        free(Rooms[i].Meshes);
    }
    free(Rooms);

    ClipCursor(nullptr);
    ShowCursor(TRUE);
}

static void FillRoomDrawInfo(const int16_t* data, DrawInfo* drawInfo)
{
    memset(drawInfo, 0, sizeof(DrawInfo));

    // vertices
    int16_t numVerts = *data++;
    drawInfo->VertexBuffer = data;
    data += numVerts * 6;

    // textured quads
    drawInfo->NumQuads = *data++;
    drawInfo->Quads = (Tr3QuadFace*)data;
    data += drawInfo->NumQuads * 5;

    // textured triangles
    drawInfo->NumTris = *data++;
    drawInfo->Tris = (Tr3TriFace*)data;

    drawInfo->Stride = 6;
}

static void FillEntityDrawInfo(const int16_t* data, DrawInfo* drawInfo)
{
    memset(drawInfo, 0, sizeof(DrawInfo));

    data += 5;

    // vertices
    int16_t numVerts = *data++;
    drawInfo->VertexBuffer = data;
    data += numVerts * 3;
    
    // normals
    int16_t numNormals = *data++;
    if (numNormals > 0) {
        data += numNormals * 3;
    }
    else {
        data += -numNormals;
    }

    // textured quads
    drawInfo->NumQuads = *data++;
    drawInfo->Quads = (Tr3QuadFace*)data;
    data += drawInfo->NumQuads * 5;

    // textures triangles
    drawInfo->NumTris = *data++;
    drawInfo->Tris = (Tr3TriFace*)data;
    data += drawInfo->NumTris * 4;

    // colored quads
    drawInfo->NumColoredQuads = *data++;
    drawInfo->ColoredQuads = (Tr3QuadFace*)data;
    data += drawInfo->NumColoredQuads * 5;

    // colored triangles
    drawInfo->NumColoredTris = *data++;
    drawInfo->ColoredTris = (Tr3TriFace*)data;
    data += drawInfo->NumColoredTris * 4;

    drawInfo->Stride = 3;
}

static void BatchTri(const int16_t* vbuf, const uint16_t* ibuf, int stride, int32_t worldX, int32_t worldY, int32_t worldZ, int16_t idx0, int16_t idx1, int16_t idx2, int16_t texId)
{
    Tr3Texture* texture = NULL;
    uint8_t* color = NULL;
    
    if (texId < NumTextures) 
    {
        static uint8_t DefaultColor[] ={ 255, 255, 255, 255 };
        texture = &Textures[texId];
        color = DefaultColor;
    }
    else
    {
        static Tr3Texture Dummy
        {
            0, MAX_TEXTURE_PAGES,
            {
                {0, 0},
                {0, TO_FIXED_8_8(32)},
                {TO_FIXED_8_8(32), 0},
                {TO_FIXED_8_8(32), TO_FIXED_8_8(32)}
            }
        };
        texture = &Dummy;
        color = Palette16 + ((texId >> 6) & 0x3fc);
    }

    uint16_t texturePage = texture->Page;
    TextureView textureView = TexturePage[texturePage];
    
    Batch& batch = PrimitiveBatches[texturePage];
    BatchUsed[texturePage] = true;

    uint32_t baseIndex = batch.NumVertices;

#define PUSH_ROOM_VERTEX(index) do {                              \
        const int16_t* vert = vbuf + ibuf[index] * stride;        \
        uint8_t r = ((vert[5] & 0x7c00) >> 10) << 3;              \
        uint8_t g = ((vert[5] & 0x03e0) >>  5) << 3;              \
        uint8_t b = ((vert[5] & 0x001f) <<  3);                   \
        assert(batch.NumVertices < MAX_BATCH_VERTICES - 1);       \
        batch.Vertices[batch.NumVertices++] =                     \
        {                                                         \
            ( vert[0] + worldX) / WorldScale,                     \
            (-vert[1] + worldY) / WorldScale,                     \
            ( vert[2] + worldZ) / WorldScale,                     \
            FIXED_8_8(texture->uv[index][0]) / textureView.Width, \
            FIXED_8_8(texture->uv[index][1]) / textureView.Height,\
            r/255.0f, g/255.0f, b/255.0f                          \
        };                                                        \
    } while(0)

#define PUSH_VERTEX(index) do {                                   \
        const int16_t* vert = vbuf + ibuf[index] * stride;        \
        uint8_t r = color[0];                                     \
        uint8_t g = color[1];                                     \
        uint8_t b = color[2];                                     \
        assert(batch.NumVertices < MAX_BATCH_VERTICES - 1);       \
        batch.Vertices[batch.NumVertices++] =                     \
        {                                                         \
            ( vert[0] + worldX) / WorldScale,                     \
            (-vert[1] + worldY) / WorldScale,                     \
            ( vert[2] + worldZ) / WorldScale,                     \
            FIXED_8_8(texture->uv[index][0]) / textureView.Width, \
            FIXED_8_8(texture->uv[index][1]) / textureView.Height,\
            r/255.0f, g/255.0f, b/255.0f                          \
        };                                                        \
    } while(0)

    if (stride == 6)
    {
        PUSH_ROOM_VERTEX(idx0);
        PUSH_ROOM_VERTEX(idx1);
        PUSH_ROOM_VERTEX(idx2);
    } 
    else
    {
        PUSH_VERTEX(idx0);
        PUSH_VERTEX(idx1);
        PUSH_VERTEX(idx2);
    }

    assert(batch.NumIndices < MAX_BATCH_INDICES - 3);
    batch.Indices[batch.NumIndices++] = baseIndex + 0;
    batch.Indices[batch.NumIndices++] = baseIndex + 1;
    batch.Indices[batch.NumIndices++] = baseIndex + 2;
}

static void BatchTris(int x, int y, int z, const int16_t* vertexBuffer, int stride, int numTris, const Tr3TriFace* tris)
{
    for (int i = 0; i < numTris; ++i)
    {
        const Tr3TriFace* tri = &tris[i];

        bool doubleSided = tri->Texture & 0x8000;

        if (doubleSided) {
            BatchTri(vertexBuffer, &tri->Vertices[0], stride, x, y, z, 0, 2, 1, tri->Texture & 0x7fff);
        }

        BatchTri(vertexBuffer, &tri->Vertices[0], stride, x, y, z, 0, 1, 2, tri->Texture & 0x7fff);
    }
}

static void BatchQuads(int x, int y, int z, const int16_t* vertexBuffer, int stride, int numQuads, const Tr3QuadFace* quads)
{
    for (int i = 0; i < numQuads; ++i)
    {
        const Tr3QuadFace* quad = &quads[i];

        bool doubleSided = quad->Texture & 0x8000;

        if (doubleSided)
        {
            BatchTri(vertexBuffer, &quad->Vertices[0], stride, x, y, z, 0, 2, 1, quad->Texture & 0x7fff);
            BatchTri(vertexBuffer, &quad->Vertices[0], stride, x, y, z, 0, 3, 2, quad->Texture & 0x7fff);
        }

        BatchTri(vertexBuffer, &quad->Vertices[0], stride, x, y, z, 0, 1, 2, quad->Texture & 0x7fff);
        BatchTri(vertexBuffer, &quad->Vertices[0], stride, x, y, z, 0, 2, 3, quad->Texture & 0x7fff);
    }
}

static void BatchDrawInfo(int x, int y, int z, const DrawInfo* drawInfo)
{
    if (drawInfo->NumQuads > 0) {
        BatchQuads(x, y, z, drawInfo->VertexBuffer, drawInfo->Stride, drawInfo->NumQuads, drawInfo->Quads);
    }
    if (drawInfo->NumTris > 0) {
        BatchTris(x, y, z, drawInfo->VertexBuffer, drawInfo->Stride, drawInfo->NumTris, drawInfo->Tris);
    }
    if (drawInfo->NumColoredQuads > 0) {
        BatchQuads(x, y, z, drawInfo->VertexBuffer, drawInfo->Stride, drawInfo->NumColoredQuads, drawInfo->ColoredQuads);
    }
    if (drawInfo->NumColoredTris > 0) {
        BatchTris(x, y, z, drawInfo->VertexBuffer, drawInfo->Stride, drawInfo->NumColoredTris, drawInfo->ColoredTris);
    }
}

static void InitializeBatches()
{
    for (int i = 0; i < TRIANGLE_BUCKETS; ++i)
    {
        PrimitiveBatches[i].NumVertices = 0;
        PrimitiveBatches[i].NumIndices = 0;;
        BatchUsed[i] = false;
    }
}

static void RenderBatches()
{
    mat4 WorldViewProjection;
    Matrix4Mul(ViewMatrix, ProjectionMatrix, WorldViewProjection);

    for (int page = 0; page < TRIANGLE_BUCKETS; ++page)
    {
        if (!BatchUsed[page]) {
            continue;
        }

        Batch* batch = &PrimitiveBatches[page];

        SetTextureView(TexturePage[page]);

        DrawTriangleList
        (
            batch->Vertices,
            batch->Indices,
            VertexLayout,
            VertexLayoutCount,
            batch->NumIndices / 3,
            WorldViewProjection,
            true
        );
    }
}

static void BatchEntity(int id, int x, int y, int z)
{
    if (Entities[id].NumMeshes == 0) {
        return;
    }

    DrawInfo drawInfo;
    for (int i = 0; i < Entities[id].NumMeshes; ++i)
    {
        int16_t* mesh = Meshes[Entities[id].MeshOffset+i];
        FillEntityDrawInfo(mesh, &drawInfo);
        BatchDrawInfo(x + i * 150, y, z, &drawInfo);
    }
}

static void RenderScene()
{
    DrawInfo drawInfo;

    if (Entities[SKYBOX].NumMeshes != 0)
    {
        int16_t* mesh = Meshes[Entities[SKYBOX].MeshOffset];
        FillEntityDrawInfo(mesh, &drawInfo);

        int x = PlayerPos[0] * WorldScale;
        int y = PlayerPos[1] * WorldScale;
        int z = PlayerPos[2] * WorldScale;
        BatchDrawInfo(x, y, z, &drawInfo);
    }

    SetDepthWrite(false);
    RenderBatches();

    // not ideal :)
    InitializeBatches();

    for (int i = 0; i < NumRooms; ++i) 
    {
        Tr3Room* room = &Rooms[i];
        FillRoomDrawInfo(room->Data, &drawInfo);

        // batch room geomtry
        BatchDrawInfo(room->x, room->y, room->z, &drawInfo);

        for (int j = 0; j < room->NumMeshes; ++j)
        {
            Tr3StaticObject* object = &StaticObjects[room->Meshes[j].MeshID];
            if (object->Flags & 2)
            {
                FillEntityDrawInfo(Meshes[object->Mesh], &drawInfo);
                BatchDrawInfo(room->Meshes[j].x,
                              -room->Meshes[j].y,
                              room->Meshes[j].z,
                              &drawInfo);
            }
        }
    }

    SetDepthWrite(true);
    RenderBatches();
}

static void MouseDelta(vec2i out)
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
    vec2i mouseDelta;
    vec3 tmp;
    MouseDelta(mouseDelta);

#if 1
    Yaw -= mouseDelta[0] * MouseSensitivity;
    Pitch += mouseDelta[1] * MouseSensitivity;
    Pitch = Clamp(Pitch, -1.5f, 1.5f);
#else
    PlayerPos[0] = 143.697220; 
    PlayerPos[1] = -128.209290; 
    PlayerPos[2] = 376.154755;
    Yaw = 2.813297; 
    Pitch = -0.045000;
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

    float speed = MoveSpeed;
    if (Thirteen::GetKey(VK_SPACE)) {
        speed *= 10;
    }

    speed = speed * DeltaTime;

    if (Thirteen::GetKey('T') && !Thirteen::GetKeyLastFrame('T'))
    {
        Trace(Format("PlayerPos[0]=%f;\nPlayerPos[1]=%f;\nPlayerPos[2]=%f;\n", PlayerPos[0], PlayerPos[1], PlayerPos[2]));
        Trace(Format("Yaw=%f;\nPitch=%f;", Yaw, Pitch));
    }

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

    if (Thirteen::GetKey('L') && !Thirteen::GetKeyLastFrame('L')) {
        LightmapsOnly = !LightmapsOnly;
    }

    vec3 target;
    Vec3Add(PlayerPos, forward, target);

    CreateMatrixLookAt(PlayerPos, target, up, ViewMatrix);

    CreateMatrixPerspectiveFovLH(45.0f, FB_WIDTH / (float)FB_HEIGHT, 1, 700.0f, ProjectionMatrix);
}

static rgba8 ShadePixelLM(float mipLevel, const Interpolants* interp)
{ 
    return COLOR(interp->r, interp->g, interp->b);
}
static rgba8 ShadePixel(float mipLevel, const Interpolants* interp)
{
#if 1
    rgba8 color = SampleTextureLod(interp->px, interp->py, interp->u, interp->v, mipLevel);
    rgba8 vertexColor = COLOR(interp->r, interp->g, interp->b);
    return RGBA8Mul(color, vertexColor);
#else
    color4 color;
    ConvertRGBA8(SampleTextureLod(interp->px, interp->py, interp->u, interp->v, mipLevel), color);

    color4 vertexColor{ interp->r, interp->g, interp->b, 1 };
    Color4Mul(color, vertexColor, color);

    return ConvertColor4(color);
#endif
}

void SceneRenderFrame()
{
    HandleInput();

    Clear(RGB(0, 0, 0));
    SetDrawMode(DrawMode::Solid);

    if (LightmapsOnly) {
        SetPixelShader(ShadePixelLM);
    }
    else {
        SetPixelShader(ShadePixel);
    }

    InitializeBatches();
    RenderScene();
}

void SceneRenderOverlay2D()
{
    WriteString(Format("%f %f %f", PlayerPos[0] * WorldScale, PlayerPos[1] * WorldScale, PlayerPos[2] * WorldScale), 0, 700);
}

#endif