#ifndef PJD_TEXTURE_LOADER_H
#define PJD_TEXTURE_LOADER_H

#include "raster_common.h"

TextureView LoadCheckerboardTexture();
TextureView LoadTexture(const char* path);
void FreeTexture(TextureView texture);

#endif // PJD_TEXTURE_LOADER_H