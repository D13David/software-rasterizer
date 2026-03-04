#ifndef PJD_RASTER_H
#define PJD_RASTER_H

#include "common.h"
#include "raster_common.h"
#include "mathlib.h"

#define DEBUG_VIEW 1

#if DEBUG_VIEW
typedef enum DebugMode
{
    DM_None,
    DM_TileClassification,
    DM_FaceDerivatives,
    DM_FaceMipMapLevel
};
#endif

void RasterizerInitialize(const RasterizerDesc& init);
void RasterizerDestroy();

void SetTextureFilter(TextureFilter filter);
void SetTextureView(TextureView texture);
void SetDrawMode(DrawMode drawMode);
#if DEBUG_VIEW
void SetDebugMode(DebugMode mode);
#endif

void Clear(Color color);
void DrawPixel(int x, int y, Color color);
void DrawPixelToScreen(int x, int y, Color color);
void DrawTriangleList(const void* data, const uint16_t* indices, const InputElement* elements, int numInputElements, int numPrimitives, mat4 proj, bool parallel);
void ResolveFrameBuffer();

/*----------------------------------------------------------------------------
    2D Drawing
----------------------------------------------------------------------------*/
void DrawLine(int x0, int y0, int x1, int y1, Color color);
void DrawRectangle(int x, int y, int w, int h, Color color);
void DrawEllipseFilled(int cx, int cy, int rx, int ry, uint32_t color, int style);

/*----------------------------------------------------------------------------
    Input Assembler
----------------------------------------------------------------------------*/
int InputStreamElementSize(const InputElement* elements, int numElements);
const InputElement* InputStreamElementByType(const InputElement* elements, int numElements, InputElementType type);
const void* InputStreamElement(const void* stream, InputElement element, int stride, int index);

#endif // PJD_RASTER_H