#include "texture_loader.h"
#include "common.h"
#include "mathlib_common.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

TextureView GNullTexture;
TextureView GCheckerBoardTexture;

#pragma pack(push, 1)

typedef struct TargaHeader {
    uint8_t     idlength;
    uint8_t     colourmaptype;
    uint8_t     datatypecode;
    uint16_t    colourmaporigin;
    uint16_t    colourmaplength;
    uint8_t     colourmapdepth;
    uint16_t    x_origin;
    uint16_t    y_origin;
    int16_t     width;
    int16_t     height;
    uint8_t     bitsperpixel;
    uint8_t     imagedescriptor;
} TargaHeader;

#pragma pack(pop)

#define UNCOMPRESSED_RGB 2

static bool LoadTextureFromFileTGA(const char* filename, int& width, int& height, void*& dataBGRA)
{
    FILE* fp;
    TargaHeader header;
    bool result;
    size_t size;

    result = true;

    fp = NULL;
    fopen_s(&fp, filename, "rb");
    if (!fp) {
        goto fail;
    }

    if (fread(&header, 1, sizeof(TargaHeader), fp) != sizeof(TargaHeader)) {
        goto fail;
    }

    if (header.datatypecode != UNCOMPRESSED_RGB) {
        goto fail;
    }

    width = header.width;
    height = header.height;

    if (header.bitsperpixel != 24 && header.bitsperpixel != 32) {
        goto fail;
    }

    size = header.width * header.height * header.bitsperpixel / 8;
    dataBGRA = (uint8_t*)malloc(size);

    if (fread(dataBGRA, 1, size, fp) != size) {
        goto fail;
    }

    goto cleanup;

fail:
    result = false;
    if (dataBGRA) {
        free(dataBGRA);
        dataBGRA = NULL;
    }
cleanup:
    if (fp) fclose(fp);

    return result;
}

static void WriteToTgaFile(const char* filename, uint32_t width, uint32_t height, uint8_t* dataBGRA, uint8_t dataChannels = 4, uint8_t fileChannels = 3)
{
    FILE* fp = NULL;
    fopen_s(&fp, filename, "wb");
    if (fp == NULL) return;

    uint8_t header[18] = { 0,0,2,0,0,0,0,0,0,0,0,0, (uint8_t)(width % 256), (uint8_t)(width / 256), (uint8_t)(height % 256), (uint8_t)(height / 256), (uint8_t)(fileChannels * 8), 0x20 };
    fwrite(&header, 18, 1, fp);

    for (uint32_t i = 0; i < width * height; i++)
    {
        for (uint32_t b = 0; b < fileChannels; b++)
        {
            fputc(dataBGRA[(i * dataChannels) + (b % dataChannels)], fp);
        }
    }
    fclose(fp);
}

static void ConvertBGRAToRGBA(uint32_t* data, int length)
{
    for (int i = 0; i < length; ++i)
    {
        data[i] = (data[i] & 0xFF00FF00) |
            ((data[i] & 0x00FF0000) >> 16) |
            ((data[i] & 0x000000FF) << 16);
    }
}

static uint32_t ComputeMipMapSize(uint32_t width, uint32_t height, uint32_t bpp, uint32_t minDimension, size_t* maxLevels)
{
    uint32_t size = 0;
    uint32_t levels = 1;
    for (; width >= minDimension && height >= minDimension; )
    {
        size += width * height * bpp;
        levels++;
        width /= 2;
        height /= 2;
    }
    if (maxLevels) *maxLevels = levels;
    return size;
}

static Color mipLevelDebugValues[20] = {
    COLOR(1.00, 0.00, 0.00),  // Red
    COLOR(0.00, 1.00, 0.00),  // Green
    COLOR(0.00, 0.00, 1.00),  // Blue
    COLOR(1.00, 1.00, 0.00),  // Yellow
    COLOR(1.00, 0.00, 1.00),  // Magenta
    COLOR(0.00, 1.00, 1.00),  // Cyan
    COLOR(1.00, 0.50, 0.00),  // Orange
    COLOR(0.50, 0.00, 1.00),  // Purple / Violet
    COLOR(0.00, 0.50, 1.00),  // Azure
    COLOR(0.50, 1.00, 0.00),  // Chartreuse
    COLOR(1.00, 0.00, 0.50),  // Rose / Pink
    COLOR(0.50, 0.50, 0.50),  // Gray (50%)
    COLOR(0.25, 0.25, 1.00),  // Periwinkle
    COLOR(1.00, 0.75, 0.00),  // Amber
    COLOR(0.75, 0.00, 1.00),  // Bright Violet
    COLOR(0.00, 0.75, 1.00),  // Sky Blue
    COLOR(0.75, 1.00, 0.00),  // Lime
    COLOR(1.00, 0.25, 0.25),  // Light Red / Salmon
    COLOR(0.25, 1.00, 0.25),  // Light Green
    COLOR(0.25, 0.25, 0.25),  // Dark Gray
};

static void GenerateMipMaps(uint8_t* data, TextureView& texture)
{
    assert(texture.Data == NULL && "texture already initialized...");
    
    size_t maxMipMapLevels;
    size_t bufferSize = ComputeMipMapSize(texture.Width, texture.Height, 4, 1, &maxMipMapLevels);

    texture.MipOffsets = (size_t*)calloc(maxMipMapLevels, sizeof(size_t));
    assert(texture.MipOffsets!= NULL);

    uint8_t* buffer = (uint8_t*)malloc(bufferSize);
    assert(buffer != NULL);

    size_t w = texture.Width;
    size_t h = texture.Height;

    memcpy(buffer, data, w * h * 4);
    texture.MipOffsets[0] = 0;

    uint8_t* prevMipMap = buffer;

    for (size_t level = 1; level < maxMipMapLevels; ++level)
    {
        uint8_t* nextMipMap = prevMipMap + (w * h * 4);

#define DEBUG_MIPMAPS 0
#if DEBUG_MIPMAPS
        for (int i = 0; i < (w / 2 * h / 2); ++i) {
            ((Color*)nextMipMap)[i] = mipLevelDebugValues[level];
        }
#else
        stbir_resize(prevMipMap, w, h, 0, nextMipMap, w / 2, h / 2, 0, STBIR_RGBA, STBIR_TYPE_UINT8, STBIR_EDGE_CLAMP, STBIR_FILTER_DEFAULT);
#endif
        w /= 2; h /= 2;

        texture.MipOffsets[level] = nextMipMap - buffer;

        prevMipMap = nextMipMap;
    }

    texture.MipLevels = maxMipMapLevels;
    texture.Data = buffer;
}

static bool GenerateCheckerboardTexture(TextureView* texture, int width, int height, int checkSize)
{
    Color* buffer = (Color*)malloc(width * height * sizeof(Color));
    if (!buffer) {
        return false;
    }

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            int cx = x / checkSize;
            int cy = y / checkSize;
            uint8_t c = ((cx + cy) & 1) ? 255 : 0;
            buffer[y * width + x] = (0xff << 24) | (c << 16) | (c << 8) | (c);
        }
    }

    texture->Width = width;
    texture->Height = height;
    
    GenerateMipMaps((uint8_t*)buffer, *texture);

    free(buffer);

    return true;
}

TextureView LoadCheckerboardTexture()
{
    // TODO: memory is never freed

    if (!GCheckerBoardTexture.Data) {
        GenerateCheckerboardTexture(&GCheckerBoardTexture, 256, 256, 8);
    }
    return GCheckerBoardTexture;
}

TextureView LoadColorTexture(Color color)
{
    TextureView result = {
        .Width = 32,
        .Height = 32
    };

    Color* buffer = (Color*)malloc(result.Width * result.Height * sizeof(Color));
    if (!buffer) {
        return GNullTexture;
    }

    for (int i = 0; i < result.Width * result.Height; ++i) {
        buffer[i] = color;
    }

    GenerateMipMaps((uint8_t*)buffer, result);

    free(buffer);

    return result;
}

TextureView LoadTexture(const char* path)
{
    // TODO: add some proper resource management here

    int width, height;
    void* loadingBuffer;
    if (!LoadTextureFromFileTGA(path, width, height, loadingBuffer))
    {
        return GNullTexture;
    }

    ConvertBGRAToRGBA((uint32_t*)loadingBuffer, width * height);

    TextureView result = {
        .Width = width,
        .Height = height,
    };

    GenerateMipMaps((uint8_t*)loadingBuffer, result);

    free(loadingBuffer);

    return result;
}

void FreeTexture(TextureView texture)
{
    // TODO: add some proper resource management here
}