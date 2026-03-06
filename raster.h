#ifndef PJD_RASTER_H
#define PJD_RASTER_H

#include "common.h"
#include "raster_common.h"
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

void RasterizerInitialize(const RasterizerDesc& init);
void RasterizerDestroy();

void SetTextureFilter(TextureFilter filter);
void SetTextureView(TextureView texture);
void SetDrawMode(DrawMode drawMode);
#if PJD_DEBUG_VIEW_ENABLED
void SetDebugMode(DebugMode mode);
#endif

void Clear(Color color);
void DrawPixel(int x, int y, Color color);
void DrawPixelToScreen(int x, int y, Color color);
void DrawTriangleList(const void* data, const uint32_t* indices, const InputElement* elements, int numInputElements, int numPrimitives, mat4 proj, bool parallel);
void ResolveFrameBuffer();

/*----------------------------------------------------------------------------
    2D Drawing
----------------------------------------------------------------------------*/
void DrawLine(int x0, int y0, int x1, int y1, Color color, uint8_t thickness = 1, LineStyle style = SOLID_LINE);
void DrawRectangle(int x, int y, int w, int h, Color color, FillStyle style = SOLID_FILL);
void DrawCircle(int cx, int cy, int radius, uint32_t color, FillStyle style = SOLID_FILL);
void DrawEllipse(int cx, int cy, int rx, int ry, uint32_t color, FillStyle style = SOLID_FILL);
void WriteString(const char* text, int posX, int posY, uint32_t color = COLOR(1,1,1));

/*----------------------------------------------------------------------------
    Input Assembler
----------------------------------------------------------------------------*/
int InputStreamElementSize(const InputElement* elements, int numElements);
const InputElement* InputStreamElementByType(const InputElement* elements, int numElements, InputElementType type);
const void* InputStreamElement(const void* stream, InputElement element, int stride, int index);

#endif // PJD_RASTER_H