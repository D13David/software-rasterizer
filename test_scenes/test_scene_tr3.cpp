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

#define RENDER_SKY 1
#define RENDER_ENTITIES 1
#define RENDER_WORLD 1

#define FOG_START   20*1024
#define FOG_END     28*1024

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

struct Tr3Portal  // 32 bytes
{
    uint16_t  AdjoiningRoom; // Which room this portal leads to
    uint16_t  nx, ny, nz;
    uint16_t  ax, ay, az;
    uint16_t  bx, by, bz;
    uint16_t  cx, cy, cz;
    uint16_t  dx, dy, dz;
};

typedef struct Tr3Room
{
    float x, y, z;
    int16_t* Data;
    Tr3Portal* Portals;
    uint16_t NumPortals;
    Tr3RoomStaticMesh* Meshes;
    uint16_t NumMeshes;
    vec3 Bounds[2];
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
static uint8_t         Palette8[256 * 3];
static uint8_t         Palette16[256 * 4];
static TextureView     TexturePage[MAX_TEXTURE_PAGES + 1];
static uint16_t        NumRooms;
static Tr3Room*        Rooms;
static Tr3Texture      Textures[MAX_TEXTURES];
static uint32_t        NumTextures;
static int16_t*        TextureAnimRecords;
static int16_t*        MeshData;
static int16_t**       Meshes;
static uint32_t        NumEntities;
static uint32_t        NumStaticObjects;
static Tr3Entity       Entities[NUM_ENTITIES];
static Tr3StaticObject StaticObjects[MAX_STATIC_OBJECTS];

static Batch        PrimitiveBatches[TRIANGLE_BUCKETS][2];

static vec3         PlayerPos;
static float        Yaw = DEG2RAD(90.0f);
static float        Pitch;
static mat4         ViewMatrix;
static mat4         ProjectionMatrix;
static mat4         ObjectToWorld;
static mat4         ObjectToView;
static int          centerX;
static int          centerY;
static int          CurrentRoom = -1;

static bool         LightmapsOnly;
static bool         AlphaTestEnabled = 1;
static bool         FogEnabled = 1;

const float         MouseSensitivity = 0.0025f;
const float         MoveSpeed = 4.0f;
const float         WorldScale = 200.0f;

InputElementDescriptor VertexLayout[] = {
        { InputElementType::Position, 0,  InputElementFormat::FLOAT3, 0,  offsetof(Vertex, x) },
        { InputElementType::Texcoord, 0,  InputElementFormat::FLOAT2, 0,  offsetof(Vertex, u) },
        { InputElementType::Color,    0,  InputElementFormat::FLOAT3, 0,  offsetof(Vertex, r) }
};
const uint32_t VertexLayoutCount = sizeof(VertexLayout) / sizeof(VertexLayout[0]);

static void FillEntityDrawInfo(const int16_t* data, DrawInfo* drawInfo);

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
    fread(&Palette8, 3, 256, fp);

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

        //WriteToTgaFile(Format("atlas%d.tga", i), 256, 256, (uint8_t*)&buffer[0]);

        TexturePage[i] = LoadTextureFromMemory(buffer, 256, 256, true);
    }

    TexturePage[MAX_TEXTURE_PAGES] = LoadColorTexture(RGBA(255, 255, 255, 255));
}

static void LoadRooms(FILE* fp)
{
    NumRooms = Read<uint16_t>(fp);

    Rooms = (Tr3Room*)malloc(sizeof(Tr3Room) * NumRooms);

    for (int i = 0; i < NumRooms; ++i)
    {
        Rooms[i].x = Read<int32_t>(fp) / WorldScale;
        Rooms[i].y = 0;
        Rooms[i].z = Read<int32_t>(fp) / WorldScale;
        
        // room min/max extend
        int ybottom = Read<int32_t>(fp);
        int ytop = Read<int32_t>(fp);

        uint32_t size = Read<uint32_t>(fp);
        Rooms[i].Data = (int16_t*)malloc(sizeof(int16_t) * size);
        fread(Rooms[i].Data, sizeof(int16_t), size, fp);

        // portals
        Rooms[i].NumPortals = Read<uint16_t>(fp);
        Rooms[i].Portals = (Tr3Portal*)malloc(sizeof(Tr3Portal) * Rooms[i].NumPortals);
        fread(Rooms[i].Portals, sizeof(Tr3Portal), Rooms[i].NumPortals, fp);

        // sector data
        uint16_t NumZSectors = Read<uint16_t>(fp);
        uint16_t NumXSectors = Read<uint16_t>(fp);
        Skip(fp, 8, NumZSectors * NumXSectors);

        float minx = Rooms[i].x;
        float miny = ybottom / WorldScale;
        float minz = Rooms[i].z;

        float maxx = Rooms[i].x + (NumXSectors * 1024) / WorldScale;
        float maxy = ytop / WorldScale;
        float maxz = Rooms[i].z + (NumZSectors * 1024) / WorldScale;

        Rooms[i].Bounds[0][0] = minx;
        Rooms[i].Bounds[0][1] = -miny;
        Rooms[i].Bounds[0][2] = minz;
        Rooms[i].Bounds[1][0] = maxx;
        Rooms[i].Bounds[1][1] = -maxy;
        Rooms[i].Bounds[1][2] = maxz;

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
    int size = Read<uint32_t>(fp);
    TextureAnimRecords = (int16_t*)malloc(size * sizeof(int16_t));
    fread(TextureAnimRecords, sizeof(int16_t), size, fp);

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

    DrawInfo drawInfo;
    if (Entities[SKYBOX].NumMeshes != 0)
    {
        int16_t* mesh = Meshes[Entities[SKYBOX].MeshOffset];
        FillEntityDrawInfo(mesh, &drawInfo);

        for (int i = 0; i < drawInfo.NumColoredTris; ++i)
        {
            Tr3TriFace* tri = &drawInfo.ColoredTris[i];
            
            const int16_t* v0 = drawInfo.VertexBuffer + tri->Vertices[0] * drawInfo.Stride;
            const int16_t* v1 = drawInfo.VertexBuffer + tri->Vertices[1] * drawInfo.Stride;
            const int16_t* v2 = drawInfo.VertexBuffer + tri->Vertices[2] * drawInfo.Stride;
            if (v0[1] > -500) {
                tri->Texture = 6<<8;
            }
        }
    }

    return true;
}

bool SceneInitialize()
{
    if (!LoadTombRaider3Level("C:\\Program Files (x86)\\Steam\\steamapps\\common\\TombRaider (III)\\data\\TEMPLE.TR2")) {
        return false;
    }

    ShowCursor(FALSE);

    centerX = GetSystemMetrics(SM_CXSCREEN) / 2;
    centerY = GetSystemMetrics(SM_CYSCREEN) / 2;

    RECT screenRect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    ClipCursor(&screenRect);

    SetCursorPos(centerX, centerY);

    FILE* fp;
    if (!fopen_s(&fp, "start_pos.txt", "r"))
    {
        fscanf(fp, "%f %f %f\n", &PlayerPos[0], &PlayerPos[1], &PlayerPos[2]);
        fscanf(fp, "%f %f\n", &Yaw, &Pitch);
        fclose(fp);
    }

    return true;
}

void SceneDestroy()
{
    free(TextureAnimRecords);
    free(Meshes);
    free(MeshData);
    for (int i = 0; i < NumRooms; ++i) {
        free(Rooms[i].Data);
        free(Rooms[i].Portals);
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

static const vec3 DebugColors[15] =
{
    {1.0f, 0.0f, 0.0f},     // red
    {0.0f, 1.0f, 0.0f},     // green
    {0.0f, 0.0f, 1.0f},     // blue
    {1.0f, 1.0f, 0.0f},     // yellow
    {1.0f, 0.0f, 1.0f},     // magenta
    {0.0f, 1.0f, 1.0f},     // cyan
    {1.0f, 0.5f, 0.0f},     // orange
    {0.5f, 0.0f, 1.0f},     // purple
    {0.0f, 0.5f, 1.0f},     // sky blue
    {0.5f, 1.0f, 0.0f},     // lime
    {1.0f, 0.0f, 0.5f},     // pink
    {0.0f, 0.5f, 0.0f},     // dark green
    {0.5f, 0.25f, 0.0f},    // brown
    {0.25f, 0.25f, 0.25f},  // gray
    {1.0f, 1.0f, 1.0f}      // white
};

static void BatchTri(const int16_t* vbuf, const uint16_t* ibuf, int stride, int16_t idx0, int16_t idx1, int16_t idx2, int16_t surfaceColorValue, bool textured)
{
    Tr3Texture* texture = NULL;
    uint8_t* defaultColor = NULL;
    
    if (textured)
    {
        static uint8_t DefaultColor[] ={ 255, 255, 255, 255 };
        texture = &Textures[surfaceColorValue & 0x7fff];
        defaultColor = DefaultColor;
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
        defaultColor = Palette16 + ((surfaceColorValue >> 6) & 0x3fc);
    }

    uint16_t texturePage = texture->Page;
    TextureView textureView = TexturePage[texturePage];
    
    Batch& batch = PrimitiveBatches[texturePage][texture->Attribute & 1];

    uint32_t baseIndex = batch.NumVertices;

    vec4 posVS[3];
    vec4 color[3];

    int16_t idx[]{ idx0,idx1,idx2 };

    for (int i = 0; i < 3; ++i)
    {
        const int16_t* vert = vbuf + ibuf[idx[i]] * stride;
        vec3 tmp = {
            ( vert[0] / WorldScale),
            (-vert[1] / WorldScale),
            ( vert[2] / WorldScale)
        };
        Matrix4MulVec3(ObjectToView, tmp, 1, posVS[i]);

        uint8_t r, g, b;
        if (stride == 6) 
        {
            r = ((vert[5] & 0x7c00) >> 10) << 3;
            g = ((vert[5] & 0x03e0) >> 5) << 3;
            b = ((vert[5] & 0x001f) << 3);
        }
        else
        {
            r = defaultColor[0];
            g = defaultColor[1];
            b = defaultColor[2];
        }

#if 0 // debug rooms
        r = DebugColors[CurrentRoom % 16][0]*255;
        g = DebugColors[CurrentRoom % 16][1]*255;
        b = DebugColors[CurrentRoom % 16][2]*255;
#endif

        if (FogEnabled)
        {
            uint16_t z = (uint16_t)(posVS[i][2] * 256.0f);
            uint32_t fog = 256;
            if (z >= FOG_END) fog = 0;
            else if (z > FOG_START) fog = 256 - ((z - FOG_START) * 256) / (FOG_END - FOG_START);

            r = (r * fog) >> 8;
            g = (g * fog) >> 8;
            b = (b * fog) >> 8;
        }

        color[i][0] = r / 255.0f;
        color[i][1] = g / 255.0f;
        color[i][2] = b / 255.0f;
    }

#define PUSH_VERTEX(index) do {                                   \
        assert(batch.NumVertices < MAX_BATCH_VERTICES - 1);       \
        batch.Vertices[batch.NumVertices++] =                     \
        {                                                         \
            posVS[index][0],                                      \
            posVS[index][1],                                      \
            posVS[index][2],                                      \
            FIXED_8_8(texture->uv[idx[index]][0]) / textureView.Width, \
            FIXED_8_8(texture->uv[idx[index]][1]) / textureView.Height,\
            color[index][0], color[index][1], color[index][2]     \
        };                                                        \
    } while(0)

    PUSH_VERTEX(0);
    PUSH_VERTEX(1);
    PUSH_VERTEX(2);

    assert(batch.NumIndices < MAX_BATCH_INDICES - 3);
    batch.Indices[batch.NumIndices++] = baseIndex + 0;
    batch.Indices[batch.NumIndices++] = baseIndex + 1;
    batch.Indices[batch.NumIndices++] = baseIndex + 2;
}

static void BatchTris(const int16_t* vertexBuffer, int stride, int numTris, const Tr3TriFace* tris, bool textured)
{
    for (int i = 0; i < numTris; ++i)
    {
        const Tr3TriFace* tri = &tris[i];

        bool doubleSided = tri->Texture & 0x8000;

        if (doubleSided) {
            BatchTri(vertexBuffer, &tri->Vertices[0], stride, 0, 2, 1, tri->Texture, textured);
        }

        BatchTri(vertexBuffer, &tri->Vertices[0], stride, 0, 1, 2, tri->Texture, textured);
    }
}

static void BatchQuads(const int16_t* vertexBuffer, int stride, int numQuads, const Tr3QuadFace* quads, bool textured)
{
    for (int i = 0; i < numQuads; ++i)
    {
        const Tr3QuadFace* quad = &quads[i];

        bool doubleSided = quad->Texture & 0x8000;

        if (doubleSided)
        {
            BatchTri(vertexBuffer, &quad->Vertices[0], stride, 0, 2, 1, quad->Texture, textured);
            BatchTri(vertexBuffer, &quad->Vertices[0], stride, 0, 3, 2, quad->Texture, textured);
        }

        BatchTri(vertexBuffer, &quad->Vertices[0], stride, 0, 1, 2, quad->Texture, textured);
        BatchTri(vertexBuffer, &quad->Vertices[0], stride,  0, 2, 3, quad->Texture, textured);
    }
}

static void BatchDrawInfo(const DrawInfo* drawInfo)
{
    if (drawInfo->NumQuads > 0) {
        BatchQuads(drawInfo->VertexBuffer, drawInfo->Stride, drawInfo->NumQuads, drawInfo->Quads, true);
    }
    if (drawInfo->NumTris > 0) {
        BatchTris(drawInfo->VertexBuffer, drawInfo->Stride, drawInfo->NumTris, drawInfo->Tris, true);
    }
    if (drawInfo->NumColoredQuads > 0) {
        BatchQuads(drawInfo->VertexBuffer, drawInfo->Stride, drawInfo->NumColoredQuads, drawInfo->ColoredQuads, false);
    }
    if (drawInfo->NumColoredTris > 0) {
        BatchTris(drawInfo->VertexBuffer, drawInfo->Stride, drawInfo->NumColoredTris, drawInfo->ColoredTris, false);
    }
}

static void InitializeBatches()
{
    for (int i = 0; i < TRIANGLE_BUCKETS; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            PrimitiveBatches[i][j].NumVertices = 0;
            PrimitiveBatches[i][j].NumIndices = 0;
        }
    }
}

static rgba8 ShadePixelLM(float mipLevel, const Interpolants* interp, bool* discard)
{
    return COLOR(interp->r, interp->g, interp->b);
}
static rgba8 ShadePixel(float mipLevel, const Interpolants* interp, bool* discard)
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
static rgba8 ShadePixelAlphaTest(float mipLevel, const Interpolants* interp, bool* discard)
{
    rgba8 color = SampleTextureLod(interp->px, interp->py, interp->u, interp->v, mipLevel);
    if (AlphaTestEnabled && color == 0) {
        *discard = true;
        return 0;
    }
    rgba8 vertexColor = COLOR(interp->r, interp->g, interp->b);
    return RGBA8Mul(color, vertexColor);
}

static void RenderBatches()
{
    if (LightmapsOnly) {
        SetPixelShader(ShadePixelLM);
    }
    else {
        SetPixelShader(ShadePixel);
    }

    // draw opaque
    for (int page = 0; page < TRIANGLE_BUCKETS; ++page)
    {
        Batch* batch = &PrimitiveBatches[page][0];
        if (batch->NumIndices == 0) {
            continue;
        }

        SetTextureView(TexturePage[page]);

        DrawTriangleList
        (
            batch->Vertices,
            batch->Indices,
            VertexLayout,
            VertexLayoutCount,
            batch->NumIndices / 3,
            ProjectionMatrix,
            true
        );
    }

    SetPixelShader(ShadePixelAlphaTest);

    // draw alpha tested
    for (int page = 0; page < TRIANGLE_BUCKETS; ++page)
    {
        Batch* batch = &PrimitiveBatches[page][1];
        if (batch->NumIndices == 0) {
            continue;
        }

        SetTextureView(TexturePage[page]);

        DrawTriangleList
        (
            batch->Vertices,
            batch->Indices,
            VertexLayout,
            VertexLayoutCount,
            batch->NumIndices / 3,
            ProjectionMatrix,
            true
        );
    }
}

static void UpdateAnimatedTextures()
{
    static float frame = 0;
    frame = frame + DeltaTime;

    typedef struct AnimRecord
    {
        int16_t NumFrames;
        int16_t FrameIds[1];
    } AnimRecord;

    if (frame > 0.1f)
    {
        int16_t* ptr = TextureAnimRecords;
        for (int i = *ptr++; i > 0; --i)
        {
            const AnimRecord* record = (const AnimRecord*)ptr++;

            Tr3Texture tmp = Textures[record->FrameIds[0]];
            for (int j = 0; j < record->NumFrames; ++j) {
                Textures[record->FrameIds[j]] = Textures[record->FrameIds[j + 1]];
            }
            Textures[record->FrameIds[record->NumFrames]] = tmp;

            ptr = ptr + (record->NumFrames + 1);
        }
        frame = 0;
    }
}

static void DrawAABB(vec3 min, vec3 max)
{
    vec3 c[8] = {
        {min[0],min[1],min[2]}, {max[0],min[1],min[2]},
        {max[0],max[1],min[2]}, {min[0],max[1],min[2]},
        {min[0],min[1],max[2]}, {max[0],min[1],max[2]},
        {max[0],max[1],max[2]}, {min[0],max[1],max[2]}
    };

    vec2 p[8];
    bool valid[8]{ 0 };

    for (int i = 0; i < 8; i++)
    {
        vec4 clip;
        mat4 ViewProjectionMatrix;
        Matrix4Mul(ViewMatrix, ProjectionMatrix, ViewProjectionMatrix);
        Matrix4MulVec3(ViewProjectionMatrix, c[i], 1, clip);

        valid[i] = clip[3] > 0.0f;
        if (!valid[i]) {
            continue;
        }

        float ndcX = clip[0] / clip[3];
        float ndcY = clip[1] / clip[3];

        p[i][0] = (ndcX * 0.5f + 0.5f) * PJD_FB_WIDTH;
        p[i][1] = (1.0f - (ndcY * 0.5f + 0.5f)) * PJD_FB_HEIGHT;
    }

    int e[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };

    for (int i = 0; i < 12; i++) {
        int a = e[i][0];
        int b = e[i][1];

        if (!valid[a] || !valid[b])
            continue;

        DrawLine(p[e[i][0]][0], p[e[i][0]][1],
                 p[e[i][1]][0], p[e[i][1]][1],
                 0xffffffff);
    }
}

static void DrawPlane(vec3 v0, vec3 v1, vec3 v2, vec3 v3)
{
    vec3 corners[4];
    vec2 p[4];
    bool valid[4] = { 0 };

    for (int i = 0; i < 3; i++) { corners[0][i] = v0[i]; }
    for (int i = 0; i < 3; i++) { corners[1][i] = v1[i]; }
    for (int i = 0; i < 3; i++) { corners[2][i] = v2[i]; }
    for (int i = 0; i < 3; i++) { corners[3][i] = v3[i]; }

    mat4 ViewProjectionMatrix;
    Matrix4Mul(ViewMatrix, ProjectionMatrix, ViewProjectionMatrix);

    for (int i = 0; i < 4; i++)
    {
        vec4 clip;
        Matrix4MulVec3(ViewProjectionMatrix, corners[i], 1, clip);

        valid[i] = clip[3] > 0.0f;
        if (!valid[i])
            continue;

        float ndcX = clip[0] / clip[3];
        float ndcY = clip[1] / clip[3];

        p[i][0] = (ndcX * 0.5f + 0.5f) * PJD_FB_WIDTH;
        p[i][1] = (1.0f - (ndcY * 0.5f + 0.5f)) * PJD_FB_HEIGHT;
    }

    int edges[4][2] = { {0,1}, {1,2}, {2,3}, {3,0} };

    for (int i = 0; i < 4; i++)
    {
        int a = edges[i][0];
        int b = edges[i][1];

        if (!valid[a] || !valid[b])
            continue;

        DrawLine(
            p[a][0], p[a][1],
            p[b][0], p[b][1],
            0xffffffff
        );
    }
}

static void RenderScene()
{
    UpdateAnimatedTextures();

    DrawInfo drawInfo;
#if RENDER_SKY
    if (Entities[SKYBOX].NumMeshes != 0)
    {
        int16_t* mesh = Meshes[Entities[SKYBOX].MeshOffset];
        FillEntityDrawInfo(mesh, &drawInfo);

        mat4 ObjectScale;
        CreateMatrixScale(1, 1, 1, ObjectScale);

        mat4 ObjectTranslate;
        CreateMatrixTransform(PlayerPos[0], PlayerPos[1], PlayerPos[2], ObjectTranslate);

        Matrix4Mul(ObjectScale, ObjectTranslate, ObjectToWorld);
        Matrix4Mul(ObjectToWorld, ViewMatrix, ObjectToView);

        BatchDrawInfo(&drawInfo);
    }
#endif // RENDER_SKY

    SetDepthWrite(false);
    RenderBatches();

    // not ideal :)
    InitializeBatches();

    for (int i = 0; i < NumRooms; ++i)
    {
        CurrentRoom = i;
        Tr3Room* room = &Rooms[i];
        FillRoomDrawInfo(room->Data, &drawInfo);

        CreateMatrixTransform(room->x, room->y, room->z, ObjectToWorld);
        Matrix4Mul(ObjectToWorld, ViewMatrix, ObjectToView);

#if RENDER_WORLD
        // batch room geomtry
        BatchDrawInfo(&drawInfo);
#endif

#if RENDER_ENTITIES
        for (int j = 0; j < room->NumMeshes; ++j)
        {
            mat4 MatObjOrient;
            CreateMatrixRotateY((room->Meshes[j].Rotation / 0x4000) * PJD_PI_2, MatObjOrient);

            mat4 MatObjTranslate;
            CreateMatrixTransform(room->Meshes[j].x / WorldScale, -room->Meshes[j].y / WorldScale, room->Meshes[j].z / WorldScale, MatObjTranslate);

            Matrix4Mul(MatObjOrient, MatObjTranslate, ObjectToWorld);
            Matrix4Mul(ObjectToWorld, ViewMatrix, ObjectToView);

            Tr3StaticObject* object = &StaticObjects[room->Meshes[j].MeshID];
            if (object->Flags & 2)
            {
                FillEntityDrawInfo(Meshes[object->Mesh], &drawInfo);
                BatchDrawInfo(&drawInfo);
            }
        }
#endif
    }

    SetDepthWrite(true);
    RenderBatches();

    //RasterMode2D(false);
    ////for (int i = 0; i < sizeof(roomList) / sizeof(int); ++i)
    //{
    //    Tr3Room& room = Rooms[roomList[0]];

    //    //DrawAABB(room.Bounds[0], room.Bounds[1]);

    //    for (int p = 0; p < room.NumPortals; ++p)
    //    {
    //        Tr3Portal& portal = room.Portals[p];
    //        vec3 a = { room.x + portal.ax / WorldScale, -portal.ay / WorldScale, room.z + portal.az / WorldScale };
    //        vec3 b = { room.x + portal.bx / WorldScale, -portal.by / WorldScale, room.z + portal.bz / WorldScale };
    //        vec3 c = { room.x + portal.cx / WorldScale, -portal.cy / WorldScale, room.z + portal.cz / WorldScale };
    //        vec3 d = { room.x + portal.dx / WorldScale, -portal.dy / WorldScale, room.z + portal.dz / WorldScale };
    //        DrawPlane(a, b, c, d);
    //    }
    //}
    //RasterMode2D(true);
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
        FILE* fp;
        fopen_s(&fp, "start_pos.txt", "w");
        fprintf_s(fp, "%f %f %f\n", PlayerPos[0], PlayerPos[1], PlayerPos[2]);
        fprintf_s(fp, "%f %f\n", Yaw, Pitch);
        fclose(fp);
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
    if (Thirteen::GetKey('K') && !Thirteen::GetKeyLastFrame('K')) {
        AlphaTestEnabled = !AlphaTestEnabled;
    }
    if (Thirteen::GetKey('F') && !Thirteen::GetKeyLastFrame('F')) {
        FogEnabled = !FogEnabled;
    }

    vec3 target;
    Vec3Add(PlayerPos, forward, target);

    CreateMatrixLookAt(PlayerPos, target, up, ViewMatrix);

    CreateMatrixPerspectiveFovLH(45.0f, PJD_FB_WIDTH / (float)PJD_FB_HEIGHT, 3, 300, ProjectionMatrix);
}

void SceneRenderFrame()
{
    HandleInput();

    Clear(RGB(255, 255, 0));

    InitializeBatches();
    RenderScene();
}

void SceneRenderOverlay2D()
{
    //WriteString(Format("Current Room=%d", CurrentRoom), 0, 700);
}

#endif