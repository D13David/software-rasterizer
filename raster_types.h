#ifndef PJD_RASTER_TYPES_H
#define PJD_RASTER_TYPES_H

#include "common.h"

typedef struct ModeDesc
{
    uint32_t    Width;
    uint32_t    Height;
} ModeDesc;

typedef struct RasterizerDesc
{
    ModeDesc    BufferDesc;
    void*       FrameBufferPtr;
    void*       DepthBufferPtr;
} RasterizerDesc;

enum class InputElementFormat
{
    FLOAT2,
    FLOAT3,
    FLOAT4,
    UINT8_4,
};

typedef enum class InputElementType
{
    Position,
    Texcoord,
    Normal,
    Color,
} InputElementType;

typedef struct InputElementDescriptor
{
    InputElementType   Type;
    uint32_t           TypeIndex;
    InputElementFormat Format;
    uint32_t           StreamIndex;
    uint32_t           Offset;
} InputElementDescriptor;

static const uint32_t APPEND_ALIGNED_ELEMENT = uint32_t(-1);

enum TextureFilter
{
    Nearest,
    Unreal
};

struct TextureView
{
    int     Width;
    int     Height;
    int     MipLevels;
    void*   Data;
    size_t* MipOffsets;
};

enum DrawMode
{
    Solid,
    Wireframe
};

typedef struct Interpolants
{
    int cx0, cx1, cx2;
    uint16_t px, py;
    float   z, u, v; 
    float   nx, ny, nz;
    float   r, g, b;
} Interpolants;

#define I(idx, attr) interpolants[idx].attr

#endif // PJD_RASTER_TYPES_H