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
#define MAX_BATCH_INDICES   60000
#define MAX_BATCH_VERTICES  60000

#define FIXED_8_8(value) (((value) >> 8) + (value & 0xff) / 256.0f) 

typedef struct Tr3Room
{
    int32_t x, y, z;
    int16_t* Data;
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
    uint16_t Attribute;
    uint16_t Page;
    uint16_t uv[4][2];
} Tr3Texture;

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

typedef struct RoomDrawInfo
{
    int16_t*        VertexBuffer;
    Tr3QuadFace*    Quads;
    Tr3TriFace*     Tris;
    int             NumQuads;
    int             NumTris;
} RoomDrawInfo;


// tomb raider 3 level data
static TextureView  TexturePage[MAX_TEXTURE_PAGES];
static uint16_t     NumRooms;
static Tr3Room*     Rooms;
static Tr3Texture   Textures[MAX_TEXTURES];

static bool         BatchUsed[MAX_TEXTURE_PAGES] = {};
static Batch        PrimitiveBatches[MAX_TEXTURE_PAGES];

static vec3         PlayerPos;
static float        Yaw = DEG2RAD(90.0f);
static float        Pitch;
static mat4         ViewMatrix;
static mat4         ProjectionMatrix;
static int          centerX;
static int          centerY;

static bool         LightmapsOnly;

const float         MouseSensitivity = 0.0025f;
const float         MoveSpeed = 10.0f;
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
    Skip(fp, 4, 256);
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
        ReadCountAndSkip<uint16_t>(fp, 20);

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
    // mesh data
    ReadCountAndSkip<uint32_t>(fp, 2);

    // mesh pointers
    ReadCountAndSkip<uint32_t>(fp, 4);

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

    // models
    ReadCountAndSkip<uint32_t>(fp, 18);

    // static meshes
    ReadCountAndSkip<uint32_t>(fp, 32);
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

    uint32_t NumTextures = Read<uint32_t>(fp);
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

    return true;
}

void SceneDestroy()
{
    for (int i = 0; i < NumRooms; ++i) {
        free(Rooms[i].Data);
    }
    free(Rooms);

    ClipCursor(nullptr);
    ShowCursor(TRUE);
}

static void FillRoomDrawInfo(const Tr3Room* room, RoomDrawInfo* info)
{
    int16_t* data = room->Data;

    int numVerts = *data++;
    info->VertexBuffer = data;

    data += numVerts * 6;
    info->NumQuads = *data++;
    info->Quads = (Tr3QuadFace*)data;

    data += info->NumQuads * 5;
    info->NumTris = *data++;
    info->Tris = (Tr3TriFace*)data;
}

static void BatchTri(int16_t* vbuf, uint16_t* ibuf, int32_t worldX, int32_t worldY, int32_t worldZ, int16_t idx0, int16_t idx1, int16_t idx2, int16_t texId)
{
    Tr3Texture* texture = &Textures[texId];
    uint16_t texturePage = texture->Page;
    TextureView textureView = TexturePage[texturePage];
    
    Batch& batch = PrimitiveBatches[texturePage];
    BatchUsed[texturePage] = true;

    uint32_t baseIndex = batch.NumVertices;

#define PUSH_VERTEX(index) do {                                   \
        int16_t* vert = vbuf + ibuf[index] * 6;                   \
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

    PUSH_VERTEX(idx0);
    PUSH_VERTEX(idx1);
    PUSH_VERTEX(idx2);

    assert(batch.NumIndices < MAX_BATCH_INDICES - 3);
    batch.Indices[batch.NumIndices++] = baseIndex + 0;
    batch.Indices[batch.NumIndices++] = baseIndex + 1;
    batch.Indices[batch.NumIndices++] = baseIndex + 2;
}

static void BatchTris(Tr3Room* room, RoomDrawInfo* drawInfo)
{
    for (int i = 0; i < drawInfo->NumTris; ++i)
    {
        Tr3TriFace* tri = &drawInfo->Tris[i];

        BatchTri(drawInfo->VertexBuffer, &tri->Vertices[0], 
            room->x, room->y, room->z, 0, 1, 2, tri->Texture & 0x7fff);
    }
}

static void BatchQuads(Tr3Room* room, RoomDrawInfo* drawInfo)
{
    for (int i = 0; i < drawInfo->NumQuads; ++i)
    {
        Tr3QuadFace* quad = &drawInfo->Quads[i];

        BatchTri(drawInfo->VertexBuffer, &quad->Vertices[0], 
            room->x, room->y, room->z, 0, 1, 2, quad->Texture & 0x7fff);

        BatchTri(drawInfo->VertexBuffer, &quad->Vertices[0], 
            room->x, room->y, room->z, 0, 2, 3, quad->Texture & 0x7fff);
    }
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

    for (int i = 0; i < MAX_TEXTURE_PAGES; ++i)
    {
        PrimitiveBatches[i].NumVertices = 0;
        PrimitiveBatches[i].NumIndices = 0;;
        BatchUsed[i] = false;
    }

    RoomDrawInfo drawInfo;
    for (int i = 0; i < NumRooms; ++i)
    {
        Tr3Room* room = &Rooms[i];
        FillRoomDrawInfo(room, &drawInfo);
        BatchTris(room, &drawInfo);
        BatchQuads(room, &drawInfo);
    }


    mat4 WorldViewProjection;
    Matrix4Mul(ViewMatrix, ProjectionMatrix, WorldViewProjection);

    for (int page = 0; page < MAX_TEXTURE_PAGES; ++page)
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

void SceneRenderOverlay2D()
{
}

#endif