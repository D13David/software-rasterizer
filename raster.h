#ifndef PJD_RASTER_H
#define PJD_RASTER_H

#include "common.h"
#include "raster_common.h"
#include "mathlib.h"

void srInitialize(const RasterizerDesc& init);

void srSetTextureFilter(TextureFilter filter);
void srSetTextureView(TextureView texture);

void srClear(Color color);
void srDrawPixel(int x, int y, Color color);
void srDrawLine(int x0, int y0, int x1, int y1, Color color);
void srDrawTriangleList(const void* data, const InputElement* elements, int numInputElements, int numPrimitives, mat4 proj);

int srInputStreamElementSize(const InputElement* elements, int numElements);
const InputElement* srInputStreamElementByType(const InputElement* elements, int numElements, InputElementType type);
const void* srInputStreamElement(const void* stream, InputElement element, int stride, int index);

#endif // PJD_RASTER_H