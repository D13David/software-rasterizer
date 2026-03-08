#ifndef PJD_RASTER_H
#define PJD_RASTER_H

#include "common.h"
#include "raster_common.h"
#include "input_stream.h"
#include "mathlib.h"

#if PJD_DEBUG_VIEW_ENABLED
typedef enum DebugMode
{
    DM_None,
    DM_TileClassification,
    DM_FaceDerivatives,
    DM_FaceMipMapLevel,
    DM_DepthBuffer
} DebugMode;
#endif

typedef rgba8 (*PS)(float mipLevel, const Interpolants* interp);

void RasterizerInitialize(const RasterizerDesc& init);
void RasterizerDestroy();

void SetTextureFilter(TextureFilter filter);
void SetTextureView(TextureView texture);
void SetDrawMode(DrawMode drawMode);
#if PJD_DEBUG_VIEW_ENABLED
void SetDebugMode(DebugMode mode);
#endif
void SetPixelShader(PS shader);

void Clear(rgba8 color);
void DrawPixel(int x, int y, rgba8 color);
void DrawPixelToScreen(int x, int y, rgba8 color);
void DrawTriangleList(const void* data, const uint32_t* indices, const InputElementDescriptor* elements, int numInputElements, int numPrimitives, mat4 proj, bool parallel);
void ResolveFrameBuffer();

void SaveScreenshot(const char* filename);

/*----------------------------------------------------------------------------
    Shader API
----------------------------------------------------------------------------*/
rgba8 SampleTextureLod(int sx, int sy, float u, float v, float mipLevel);

/*----------------------------------------------------------------------------
    2D Drawing
----------------------------------------------------------------------------*/
void DrawLine(int x0, int y0, int x1, int y1, rgba8 color, uint8_t thickness = 1, LineStyle style = SOLID_LINE);
void DrawRectangle(int x, int y, int w, int h, rgba8 color, FillStyle style = SOLID_FILL);
void DrawCircle(int cx, int cy, int radius, uint32_t color, FillStyle style = SOLID_FILL);
void DrawEllipse(int cx, int cy, int rx, int ry, uint32_t color, FillStyle style = SOLID_FILL);
void WriteString(const char* text, int posX, int posY, uint32_t color = COLOR(1,1,1));

#endif // PJD_RASTER_H