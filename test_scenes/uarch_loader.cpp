#include "uarch_loader.h"
#include "common/common.h"
#include "renderer/mesh.h"
#include "math/mathlib.h"

// Reference - https://web.archive.org/web/20130208140651/http://hyper.dnsalias.net/infobase/archive/unrealtech/Packages.htm
//           - https://de.scribd.com/document/54572848/UT-Package-File-Format
//           - Unreal Public Headers

// Core/Inc/UnObjVer.h
// Prevents incorrect files from being loaded.
#define UNREAL_PACKAGE_TAG 0x9E2A83C1

// The current Unrealfile version.
#define PACKAGE_FILE_VERSION  61

#define NAME_SIZE 64

#define MAX_ARCHIVES 16

using Index = int32_t;

typedef struct TableRef
{
    uint32_t        Count;
    uint32_t        Offset;
} TableRef;

typedef struct NameEntry
{
    char            Name[NAME_SIZE];
    uint32_t        Flags;
} NameEntry;

typedef struct ImportEntry
{
    Index           ClassPackage;  // The name of the package which this object's class object resides in.
    Index           ClassName;     // The name of this object's class.
    int32_t         PackageIndex;  // The index of the package this object resides in.
    Index           ObjectName;    // The name of this object.
} ImportEntry;

typedef struct ExportEntry
{
    Index           ClassIndex;    // Points to the class object describing the class of this object.
    Index           SuperIndex;    // If this is a field (a struct, class, property, or another field subclass): Points to the superfield object of the field.
    uint32_t        PackageIndex;  // Points to the package object describing the package this object resides in.
    Index           ObjectName;    // This object's name.
    uint32_t        ObjectFlags;   // Flags.
    Index           SerialSize;    // Size (in bytes) of the object's serialized data stored in this file.
    // if SerialSize>=0
    Index           SerialOffset;  // Offset into this file of the start of the object's serialized data.
    ExportEntry*    Next;
} ExportEntry;

typedef struct UArchHeader
{
    uint32_t        Tag;            // Always 0x9E2A83C1 (see PACKAGE_FILE_TAG in UnObjVer.h).
    uint32_t        FileVersion;    // Version of the engine which saved the file. Currently 61. Utilities should only operate on files with this exact version number, because only the file header is guaranteed not to change in future versions...anything else could change (See PACKAGE_FILE_VERSION in UnObjVer.h).
    uint32_t        PackageFlags;   // Bitflags describing the package
    TableRef        NameTable;
    TableRef        ExportTable;
    TableRef        ImportTable;
} UArchHeader;

typedef struct Archive
{
    UArchHeader     Header;
    NameEntry*      NameTable;
    ImportEntry*    ImportTable;
    ExportEntry*    ExportTable;
    FILE*           FilePointer;
} Archive;

Archive ArchivePool[MAX_ARCHIVES];

#define CHECKED(expr) do {  \
        if (!(expr)) {      \
            assert(false && "FAILED: " #expr); \
        }                   \
    } while(0)

static PJD_INLINE ArchiveHandle FindFreeHandle()
{
    for (int i = 0; i < MAX_ARCHIVES; ++i)
    {
        if (ArchivePool[i].FilePointer == NULL) {
            return &ArchivePool[i];
        }
    }
    return NULL;
}

template<typename T>
static T Read(FILE* fp)
{
    T result;
    assert(fp != NULL);
    CHECKED(fread(&result, sizeof(T), 1, fp) == 1);
    return result;
}

static int ReadNameEntry(FILE* fp, NameEntry* nameEntry)
{
    assert(fp != NULL);

    memset(nameEntry->Name, 0, NAME_SIZE);
    for (int i = 0; i < NAME_SIZE; ++i)
    {
        char c = fgetc(fp);
        if (c == EOF) {
            goto error;
        }
        if (c == 0) {
            break;
        }
        nameEntry->Name[i] = c;
    }

    if (fread(&nameEntry->Flags, sizeof(uint32_t), 1, fp) != 1) {
        goto error;
    }

    return 1;

error:
    return 0;
}

static int ReadCompactIndex(FILE* fp)
{
    uint8_t b0 = (uint8_t)fgetc(fp);

    int value = b0 & 0x3F;

    if (b0 & 0x40)
    {
        uint8_t b1 = (uint8_t)fgetc(fp);
        value = value | (b1 & 0x7F) << 6;
        if (b1 & 0x80)
        {
            uint8_t b2 = (uint8_t)fgetc(fp);
            value = value | (b2 & 0x7F) << 13;
            if (b2 & 0x80)
            {
                uint8_t b3 = (uint8_t)fgetc(fp);
                value = value | (b3 & 0x7F) << 20;
                if (b3 & 0x80)
                {
                    uint8_t b4 = (uint8_t)fgetc(fp);
                    value = value | b4 << 27;
                }
            }
        }
    }

    return (b0 & 0x80) ? -value : value;
}

static void ResolveObjectClass(const ExportEntry* entry, const ArchiveHandle archive, const char** className, const char** classPackage)
{
    /**
    * Resolve object reference as per official documentation: unrealtech/Packages.htm
    * If Index==0: The object is NULL (known as NULL in C++, None in UnrealScript).
    * If Index<0: Refers to the (-Index-1)th object in this file's import table.
    * If Index>0: Refers to the (Index-1)th object in this file's export table.
    */
    if (entry->ClassIndex < 0)
    {
        const ImportEntry* Import = &archive->ImportTable[-entry->ClassIndex - 1];
        *className = archive->NameTable[Import->ObjectName].Name;

        assert(Import->PackageIndex < 0);
        Import = &archive->ImportTable[-Import->PackageIndex - 1];
        *classPackage = archive->NameTable[Import->ObjectName].Name;
    }
    else if (entry->ClassIndex > 0)
    {
        const ExportEntry* Export = &archive->ExportTable[entry->ClassIndex - 1];
        *className = archive->NameTable[Export->ObjectName].Name;
        *classPackage = "Self";
    }
    else
    {
        *className = "Class";
        *classPackage = "Core";
    }
}

ArchiveHandle OpenArchive(const char* filename)
{
    FILE* fp = NULL;
    ArchiveHandle archive = NULL;

    if (fopen_s(&fp, filename, "rb") != 0) {
        goto error;
    }

    archive = FindFreeHandle();
    if (archive == NULL) {
        goto error;
    }

    archive->FilePointer = fp;

    if (fread(&archive->Header, sizeof(UArchHeader), 1, fp) != 1) {
        goto error;
    }

    if (archive->Header.Tag != UNREAL_PACKAGE_TAG || archive->Header.FileVersion != PACKAGE_FILE_VERSION) {
        goto error;
    }

    if (archive->Header.NameTable.Count > 0)
    {
        archive->NameTable = (NameEntry*)calloc(sizeof(NameEntry), archive->Header.NameTable.Count);
        assert(archive->NameTable != NULL);

        fseek(fp, archive->Header.NameTable.Offset, SEEK_SET);

        for (uint32_t i = 0; i < archive->Header.NameTable.Count; ++i)
        {
            NameEntry* nameEntry = &archive->NameTable[i];
            ReadNameEntry(fp, nameEntry);
        }
    }

    if (archive->Header.ImportTable.Count > 0)
    {
        archive->ImportTable = (ImportEntry*)calloc(sizeof(ImportEntry), archive->Header.ImportTable.Count + 1);
        assert(archive->ImportTable != NULL);

        fseek(fp, archive->Header.ImportTable.Offset, SEEK_SET);

        for (uint32_t i = 0; i < archive->Header.ImportTable.Count; ++i)
        {
            ImportEntry* importEntry  = &archive->ImportTable[i];
            importEntry->ClassPackage = ReadCompactIndex(fp);
            importEntry->ClassName    = ReadCompactIndex(fp);
            importEntry->PackageIndex = Read<int32_t>(fp);
            importEntry->ObjectName   = ReadCompactIndex(fp);
        }
    }

    if (archive->Header.ExportTable.Count > 0)
    {
        archive->ExportTable = (ExportEntry*)calloc(sizeof(ExportEntry), archive->Header.ExportTable.Count);
        assert(archive->ExportTable != NULL);

        ExportEntry* prev = NULL;
        for (int i = archive->Header.ExportTable.Count - 1; i >= 0; --i)
        {
            archive->ExportTable[i].Next = prev;
            prev = &archive->ExportTable[i];
        }

        fseek(fp, archive->Header.ExportTable.Offset, SEEK_SET);

        for (uint32_t i = 0; i < archive->Header.ExportTable.Count; ++i)
        {
            ExportEntry* exportEntry  = &archive->ExportTable[i];
            exportEntry->ClassIndex   = ReadCompactIndex(fp);
            exportEntry->SuperIndex   = ReadCompactIndex(fp);
            exportEntry->PackageIndex = Read<uint32_t>(fp);
            exportEntry->ObjectName   = ReadCompactIndex(fp);
            exportEntry->ObjectFlags  = Read<uint32_t>(fp);
            exportEntry->SerialSize   = ReadCompactIndex(fp);
            if (exportEntry->SerialSize) {
                exportEntry->SerialOffset = ReadCompactIndex(fp);
            }
        }
    }
    
    return archive;

error:
    if (fp) fclose(fp);
    if (archive) {
        archive->FilePointer = NULL;
        archive = NULL;
    }

    return NULL;
}

void CloseArchive(ArchiveHandle handle)
{
    if (handle == NULL) {
        return;
    }

    free(handle->NameTable);
    free(handle->ImportTable);
    free(handle->ExportTable);

    if (handle->FilePointer) {
        fclose(handle->FilePointer);
        handle->FilePointer = NULL;
    }
}

void DumpExportTable(ArchiveHandle archive, const char* className)
{
    assert(archive != NULL);

    for (ExportEntry* Export = archive->ExportTable; Export; Export = Export->Next)
    {
        const char* Cls;
        const char* Pkg;
        ResolveObjectClass(Export, archive, &Cls, &Pkg);

        if (!className || strcmp(Cls, className) == 0) {
            Trace("%s\n", archive->NameTable[Export->ObjectName]);
        }
    }
}

const struct ExportEntry* FindExportByName(ArchiveHandle archive, const char* name, const char* className)
{
    assert(archive != NULL);

    for (ExportEntry* Export = archive->ExportTable; Export; Export = Export->Next)
    {
        if (strcmp(archive->NameTable[Export->ObjectName].Name, name) == 0) 
        {
            const char* Cls;
            const char* Pkg;
            ResolveObjectClass(Export, archive, &Cls, &Pkg);

            if (strcmp(Cls, className) == 0) {
                return Export;
            }
        }
    }

    return NULL;
}

int LoadMeshFromArchive(ArchiveHandle archive, const char* name, Mesh** outMesh)
{
    const ExportEntry* Export = FindExportByName(archive, name, "Mesh");
    if (!Export) {
        return 0;
    }

    fseek(archive->FilePointer, Export->SerialOffset, SEEK_SET);

    // Skip first byte, no idea what it is for
    Read<uint8_t>(archive->FilePointer);

    // read bounding box
    vec3 boundsMin, boundsMax;
    fread(boundsMin, sizeof(vec3), 1, archive->FilePointer);
    fread(boundsMax, sizeof(vec3), 1, archive->FilePointer);
    Read<uint8_t>(archive->FilePointer); // Bounds Valid

    // read bounding sphere
    vec3 boundingSphere;
    fread(boundingSphere, sizeof(vec3), 1, archive->FilePointer);

    // read vertex buffer/keyframes -------------------------------------------------------------
    const uint32_t vertexSize = sizeof(vec3) + sizeof(vec2);
    const uint32_t stride = vertexSize / sizeof(float);
    const float scale = 0.3f;

    int numVertices = ReadCompactIndex(archive->FilePointer);
    
    float* vertexBuffer = (float*)malloc(vertexSize * numVertices);
    assert(vertexBuffer);

    // load vertice for all keyframes into a buffer. we can't load directly to our mesh because
    // we have to split vertices with different tex-coords
    for (int i = 0; i < numVertices; ++i)
    {
        struct FVertex 
        {
            int32_t X:11; int32_t Y:11; int32_t Z:10;
        } V;
        fread((uint32_t*)&V, sizeof(uint32_t), 1, archive->FilePointer);

        float* target = &vertexBuffer[i * stride];

        // position
        *target++ = (V.X     ) * scale;
        *target++ = (V.Y     ) * scale;
        *target++ = (V.Z << 1) * scale;

        // texcoord
        *target++ = 0;
        *target++ = 0;
    }

    // read indices -----------------------------------------------------------------------------
    int numTriangles = ReadCompactIndex(archive->FilePointer);

    typedef struct VertexMapEntry
    {
        uint32_t    key;
        uint32_t    value;
        uint8_t     used;
    } VertexMapEntry;

    const int mapSize = numTriangles * 6;
    VertexMapEntry* map = (VertexMapEntry*)calloc(mapSize, sizeof(VertexMapEntry));
    assert(map);

    uint16_t* reverseRemap = (uint16_t*)malloc(numTriangles * 3 * sizeof(uint16_t));
    assert(reverseRemap);

    uint32_t* indexBuffer = (uint32_t*)malloc(numTriangles * 3 * sizeof(uint32_t));
    assert(indexBuffer);

    uint32_t newVertexCount = 0;
    uint32_t* indexPtr = indexBuffer;

    for (int i = 0; i < numTriangles; ++i)
    {
        uint16_t indices[3];
        fread(indices, sizeof(uint16_t), 3, archive->FilePointer);

        for (int j = 0; j < 3; ++j)
        {
            uint8_t tex[2];
            fread(tex, sizeof(uint8_t), 2, archive->FilePointer);

            uint16_t originalIndex = indices[j];
            uint32_t key = (originalIndex << 16) | (tex[0] << 8) | tex[1];
            uint32_t hash = key % mapSize;

            while (map[hash].used && map[hash].key != key)
                hash = (hash + 1) % mapSize;

            uint16_t finalIndex;

            if (!map[hash].used)
            {
                map[hash].used = 1;
                map[hash].key = key;
                map[hash].value = newVertexCount;

                reverseRemap[newVertexCount] = originalIndex;

                finalIndex = newVertexCount++;
            }
            else
            {
                finalIndex = map[hash].value;
            }

            *indexPtr++ = finalIndex;

        }

        Read<uint32_t>(archive->FilePointer); // PolyFlags
        Read<uint32_t>(archive->FilePointer); // TextureIndex
    }

    // read anim sequences ----------------------------------------------------------------------
    int numAnimSequences = ReadCompactIndex(archive->FilePointer);

    MeshAnimSeq* animSeqs = (MeshAnimSeq*)malloc(numAnimSequences * sizeof(MeshAnimSeq));
    assert(animSeqs);

    for (int i = 0; i < numAnimSequences; ++i)
    {
        MeshAnimSeq* animSeq = &animSeqs[i];
        uint32_t name       = ReadCompactIndex(archive->FilePointer);
        /* Group */           ReadCompactIndex(archive->FilePointer);
        animSeq->StartFrame = Read<int32_t>(archive->FilePointer);
        animSeq->NumFrames  = Read<int32_t>(archive->FilePointer);

        strcpy_s(animSeq->Name, 16, archive->NameTable[name].Name);

        int numNotifies = ReadCompactIndex(archive->FilePointer);
        for (int j = 0; j < numNotifies; ++j)
        {
            Read<float>(archive->FilePointer);      // Time
            ReadCompactIndex(archive->FilePointer); // Function
        }

        animSeq->Rate = Read<float>(archive->FilePointer);

#if _DEBUG
        /*Trace("#%d: %s (%d, %d, %f)\n", i, 
            animSeq->Name,
            animSeq->StartFrame,
            animSeq->NumFrames,
            animSeq->Rate);*/
#endif
    }

    // Skip this part
    int offset = 4 * sizeof(uint32_t) + // frameverts, animframes, and-flags, or-flags
        2 * sizeof(vec3) +              // scale, origin
        3 * sizeof(uint32_t) +          // rotator
        2 * sizeof(uint32_t);           // curpoly, curvertex
    fseek(archive->FilePointer, (Export->SerialOffset + Export->SerialSize) - offset, SEEK_SET);

    uint32_t oldNumVertsPerFrame = Read<uint32_t>(archive->FilePointer);
    uint32_t totalFrames = numVertices / oldNumVertsPerFrame;

    // create mesh
    Mesh* mesh = MeshCreate(vertexSize, newVertexCount * totalFrames, 1);
    *outMesh = mesh;

    // position (float3)
    mesh->InputDesc[0].Type = InputElementType::Position;
    mesh->InputDesc[0].Format = InputElementFormat::FLOAT3;
    mesh->InputDesc[0].Offset = 0;

    // texcoord (float2)
    mesh->InputDesc[1].Type = InputElementType::Texcoord;
    mesh->InputDesc[1].Format = InputElementFormat::FLOAT2;
    mesh->InputDesc[1].Offset = mesh->InputDesc[0].Offset + 12;
    mesh->NumInputElements = 2;

    // recreate keyframe buffer
    float* oldVB = vertexBuffer;

    float* newVB = (float*)mesh->VertexBuffer;

    for (uint32_t frame = 0; frame < totalFrames; ++frame)
    {
        for (uint32_t newIndex = 0; newIndex < newVertexCount; ++newIndex)
        {
            uint32_t originalIndex = reverseRemap[newIndex];

            float* src = oldVB +
                frame * oldNumVertsPerFrame * stride +
                originalIndex * stride;

            float* dst = newVB +
                frame * newVertexCount * stride +
                newIndex * stride;

            *dst++ = *src++;
            *dst++ = *src++;
            *dst++ = *src++;

            uint32_t key = 0;
            for (int k = 0; k < mapSize; ++k)
            {
                if (map[k].used && map[k].value == newIndex)
                {
                    key = map[k].key;
                    break;
                }
            }

            *dst++ = ((key >> 8) & 0xFF) / 256.0f;
            *dst++ = (key & 0xFF) / 256.0f;
        }
    }

    mesh->Surfaces[0].IndexBuffer = indexBuffer;
    mesh->Surfaces[0].NumPrimitives = numTriangles;

    mesh->AnimSeqs = animSeqs;
    mesh->NumAnimSeqs = numAnimSequences;

    mesh->NumVertsPerFrame = newVertexCount;

    free(vertexBuffer);
    free(map);
    free(reverseRemap);

    return 1;
}
