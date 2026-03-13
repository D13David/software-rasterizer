#ifndef PJD_RASTER_COMMON_H
#define PJD_RASTER_COMMON_H

#include "math/mathlib.h"
#include "raster_types.h"

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

typedef enum LineStyle
{
    SOLID_LINE,
    DOTTED_LINE,
    CENTER_LINE,
    DASHED_LINE
} LineStyle;

#endif // PJD_RASTER_COMMON_H