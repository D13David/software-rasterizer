#ifndef PJD_VEC3_H
#define PJD_VEC3_H

#include "mathlib_common.h"

PJD_INLINE void Vec3Mul(vec3 v, float s, vec3 out)
{
    out[0] = v[0] * s;
    out[1] = v[1] * s;
    out[2] = v[2] * s;
}

PJD_INLINE void Vec3Add(vec3 a, vec3 b, vec3 out)
{
    out[0] = a[0] + b[0];
    out[1] = a[1] + b[1];
    out[2] = a[2] + b[2];
}

PJD_INLINE void Vec3Sub(vec3 a, vec3 b, vec3 out)
{
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

PJD_INLINE void Vec3Cross(vec3 a, vec3 b, vec3 out)
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

PJD_INLINE float Vec3Dot(vec3 a, vec3 b)
{
    return (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
}

PJD_INLINE void Vec3NormalizeSelf(vec3 a)
{
    float length = sqrtf(Vec3Dot(a, a));
    if (length == 0) {
        return;
    }

    float invLength = 1.0f / length;

    a[0] = a[0] * invLength;
    a[1] = a[1] * invLength;
    a[2] = a[2] * invLength;
}

#endif // PJD_VEC3_H