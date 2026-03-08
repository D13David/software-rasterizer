#ifndef PJD_MATHLIB_TYPES_H
#define PJD_MATHLIB_TYPES_H

#include "common.h"

typedef int                 vec2i[2];
typedef int                 vec3i[3];
typedef int                 vec4i[4];

typedef float               vec2[2];
typedef float               vec3[3];
typedef PJD_ALIGN(16) float vec4[4];
typedef vec3                mat3[3];
typedef PJD_ALIGN(16) vec4  mat4[4];

typedef uint32_t            rgba8;
typedef float               color4[4];

#endif // PJD_MATHLIB_TYPES_H