#ifndef PJD_RASTER_TYPES_H
#define PJD_RASTER_TYPES_H

#include "common.h"

typedef uint32_t    Color;
typedef float       Colorf;

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

enum InputElementFormat
{
    FormatRG32F,
    FormatRGB32F,
    FormatRGBA32F
};

enum InputElementType
{
    TypePosition,
    TypeColor,
    TypeTexcoord
};

struct InputElement
{
    InputElementType Type;
    InputElementFormat Format;
    uint32_t Offset;
};

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

#endif // PJD_RASTER_TYPES_H