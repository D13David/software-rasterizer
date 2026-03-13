#ifndef PJD_COLOR4_H
#define PJD_COLOR4_H

#include "mathlib_common.h"

#define RGBA_UNPACK(c, r, g, b, a)        \
        uint8_t (b) = (c >>  0) & 0xff;   \
        uint8_t (g) = (c >>  8) & 0xff;   \
        uint8_t (r) = (c >> 16) & 0xff;   \
        uint8_t (a) = (c >> 24) & 0xff;

#define RGBA_PACK(r, g, b, a) \
    (((uint32_t)(a) << 24) |  \
     ((uint32_t)(r) << 16) |  \
     ((uint32_t)(g) << 8)  |  \
     ((uint32_t)(b)) )

#define RGBA(r, g, b, a) RGBA_PACK(r, g, b, a)
#define COLOR(r, g, b) ConvertColor4(r, g, b, 1)
#define COLOR4(r, g, b, a) ConvertColor4(r, g, b, a)

PJD_INLINE void ConvertRGBA8(rgba8 c, color4 out)
{
    RGBA_UNPACK(c, r, g, b, a);
    const float scale = (1.0f / 255.0f);
    out[0] = r * scale;
    out[1] = g * scale;
    out[2] = b * scale;
    out[3] = a * scale;
}

PJD_INLINE rgba8 ConvertColor4(float r, float g, float b, float a)
{
    return RGBA_PACK(r * 255, g * 255, b * 255, a * 255);
}

PJD_INLINE rgba8 ConvertColor4(color4 c)
{
    return RGBA_PACK(c[0] * 255, c[1] * 255, c[2] * 255, c[3] * 255);
}

PJD_INLINE void Color4Add(color4 a, color4 b, color4 out)
{
    out[0] = a[0] + b[0];
    out[1] = a[1] + b[1];
    out[2] = a[2] + b[2];
    out[3] = a[3] + b[3];
}

PJD_INLINE void Color4Mul(color4 a, color4 b, color4 out)
{
    out[0] = a[0] * b[0];
    out[1] = a[1] * b[1];
    out[2] = a[2] * b[2];
    out[3] = a[3] * b[3];
}

PJD_INLINE rgba8 RGBA8Mul(rgba8 c1, rgba8 c2)
{
    uint8_t a = ((c1 >> 24) * (c2 >> 24) + 127) / 255;
    uint8_t r = (((c1 >> 16) & 0xFF) * ((c2 >> 16) & 0xFF) + 127) / 255;
    uint8_t g = (((c1 >> 8) & 0xFF) * ((c2 >> 8) & 0xFF) + 127) / 255;
    uint8_t b = ((c1 & 0xFF) * (c2 & 0xFF) + 127) / 255;

    return (a << 24) | (r << 16) | (g << 8) | b;
}

PJD_INLINE void Color4Mul(color4 c, float s, color4 out)
{
    out[0] = c[0] * s;
    out[1] = c[1] * s;
    out[2] = c[2] * s;
    out[3] = c[3] * s;
}

PJD_INLINE void Color4SaturateSelf(color4 c)
{
    c[0] = Saturate(c[0]);
    c[1] = Saturate(c[1]);
    c[2] = Saturate(c[2]);
    c[3] = Saturate(c[3]);
}

PJD_INLINE rgba8 InvertRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return RGBA_PACK(255 - r, 255 - g, 255 - b, a);
}

PJD_INLINE rgba8 InvertRGBA8(rgba8 c)
{
    return c ^ 0x00FFFFFF;
}

PJD_INLINE void InvertColor4(color4 c, color4 out)
{
    out[0] = 1.0f - c[0];
    out[1] = 1.0f - c[1];
    out[2] = 1.0f - c[2];
    out[3] = c[3];
}

PJD_INLINE void GrayscaleColor4(color4 c, color4 out)
{
    float value = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
    out[0] = value;
    out[1] = value;
    out[2] = value;
    out[3] = c[3];
}

PJD_INLINE rgba8 GrayscaleRGBA8(rgba8 c)
{
    RGBA_UNPACK(c, r, g, b, a);
    uint8_t value = (77u * r + 150u * g + 28u * b) >> 8;
    return RGBA_PACK(value, value, value, a);
}

PJD_INLINE void BlendColor4(color4 a, color4 b, float t, color4 out)
{
    out[0] = Lerp(a[0], b[0], t);
    out[1] = Lerp(a[1], b[1], t);
    out[2] = Lerp(a[2], b[2], t);
    out[3] = Lerp(a[3], b[3], t);
}

PJD_INLINE rgba8 BlendRGBA8(rgba8 a, rgba8 b, float t)
{
    RGBA_UNPACK(a, r0, g0, b0, a0);
    RGBA_UNPACK(b, r1, g1, b1, a1);
    uint8_t alpha = (uint8_t)(t * 255);
    return RGBA_PACK
    (
        (uint8_t)((r0 * (255 - alpha) + r1 * alpha) >> 8),
        (uint8_t)((g0 * (255 - alpha) + g1 * alpha) >> 8),
        (uint8_t)((b0 * (255 - alpha) + b1 * alpha) >> 8),
        (uint8_t)((a0 * (255 - alpha) + a1 * alpha) >> 8)
    );
}

#endif // PJD_COLOR4_H