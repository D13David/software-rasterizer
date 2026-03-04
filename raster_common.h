#ifndef PJD_RASTER_COMMON_H
#define PJD_RASTER_COMMON_H

#include "raster_types.h"

#define RGBA(r, g, b, a) \
    ( ((uint32_t)(a) << 24) | \
      ((uint32_t)(b) << 16) | \
      ((uint32_t)(g) << 8)  | \
      ((uint32_t)(r)) )

#define COLOR(r, g, b) \
    RGBA( (uint8_t)((r) * 255.0f), \
          (uint8_t)((g) * 255.0f), \
          (uint8_t)((b) * 255.0f), \
          255 )

typedef enum FillStyle
{
    EMPTY_FILL,
    SOLID_FILL,
    LINE_FILL,
    LTSLASH_FILL,
    SLASH_FILL,
    BKSLASH_FILL,
    LTBKSLASH_FILL,
    HATCH_FILL,
    XHATCH_FILL,
    INTERLEAVE_FILL,
    WIDE_DOT_FILL,
    CLOSE_DOT_FILL
} FillStyle;

#endif // PJD_RASTER_COMMON_H