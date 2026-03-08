#ifndef PJD_TEXTURE_LOADER_H
#define PJD_TEXTURE_LOADER_H

#include "raster_common.h"

TextureView LoadCheckerboardTexture();
TextureView LoadColorTexture(rgba8 color);
TextureView LoadTexture(const char* path);
void FreeTexture(TextureView texture);
void WriteToTgaFile(const char* filename, uint32_t width, uint32_t height, uint8_t* dataRGRA, uint8_t dataChannels = 4, uint8_t fileChannels = 3);

#endif // PJD_TEXTURE_LOADER_H