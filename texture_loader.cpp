#include "texture_loader.h"
#include "common.h"

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
    texture->Data = buffer;

    return true;
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

    result.Data = buffer;

    return result;
}

TextureView LoadTexture(const char* path)
{
    // TODO: add some proper resource management here

    TextureView result;
    if (!LoadTextureFromFileTGA(path, result.Width, result.Height, result.Data))
    {
        return GNullTexture;
    }

    ConvertBGRAToRGBA((uint32_t*)result.Data, result.Width * result.Height);

    return result;
}

void FreeTexture(TextureView texture)
{
    // TODO: add some proper resource management here
}