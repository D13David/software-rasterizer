#ifndef PJD_MAT4_H
#define PJD_MAT4_H

#include "mathlib_common.h"
#include "mathlib_intrinsics.h"

PJD_INLINE void Matrix4Mul(mat4 m1, mat4 m2, mat4 out)
{
#if __AVX__
    __m128 a0 = _mm_load_ps(m1[0]);
    __m128 a1 = _mm_load_ps(m1[1]);
    __m128 a2 = _mm_load_ps(m1[2]);
    __m128 a3 = _mm_load_ps(m1[3]);

    for (int i = 0; i < 4; ++i)
    {
        __m128 acc;
        acc = _mm_mul_ps(  a0, _mm_broadcast_ss(&m2[i][0])     );
        acc = _mm_fmadd_ps(a1, _mm_broadcast_ss(&m2[i][1]), acc);
        acc = _mm_fmadd_ps(a2, _mm_broadcast_ss(&m2[i][2]), acc);
        acc = _mm_fmadd_ps(a3, _mm_broadcast_ss(&m2[i][3]), acc);
        _mm_store_ps(out[i], acc);
    }
#else
    for (int i = 0; i < 4; i++)
    {
        out[i][0] = m1[0][0] * m2[i][0] + m1[1][0] * m2[i][1] + m1[2][0] * m2[i][2] + m1[3][0] * m2[i][3];
        out[i][1] = m1[0][1] * m2[i][0] + m1[1][1] * m2[i][1] + m1[2][1] * m2[i][2] + m1[3][1] * m2[i][3];
        out[i][2] = m1[0][2] * m2[i][0] + m1[1][2] * m2[i][1] + m1[2][2] * m2[i][2] + m1[3][2] * m2[i][3];
        out[i][3] = m1[0][3] * m2[i][0] + m1[1][3] * m2[i][1] + m1[2][3] * m2[i][2] + m1[3][3] * m2[i][3];
    }
#endif
}

PJD_INLINE void Matrix4MulVec(mat4 m, vec4 v, vec4 out)
{
#if __AVX__
    __m128 tmp = _mm_set_ps(v[3], v[2], v[1], v[0]);

    __m128 m0 = _mm_load_ps(m[0]);
    __m128 m1 = _mm_load_ps(m[1]);
    __m128 m2 = _mm_load_ps(m[2]);
    __m128 m3 = _mm_load_ps(m[3]);

    __m128 r0 = _mm_dp_ps(m0, tmp, 0xF1);
    __m128 r1 = _mm_dp_ps(m1, tmp, 0xF2);
    __m128 r2 = _mm_dp_ps(m2, tmp, 0xF4);
    __m128 r3 = _mm_dp_ps(m3, tmp, 0xF8);

    __m128 result = _mm_or_ps(
        _mm_or_ps(r0, r1),
        _mm_or_ps(r2, r3)
    );

    _mm_store_ps(out, result);
#else
    out[0] = m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2] + m[0][3] * v[3];
    out[1] = m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2] + m[1][3] * v[3];
    out[2] = m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2] + m[2][3] * v[3];
    out[3] = m[3][0] * v[0] + m[3][1] * v[1] + m[3][2] * v[2] + m[3][3] * v[3];
#endif
}

PJD_INLINE void Matrix4MulVec3(mat4 m, vec3 v, float w, vec4 out)
{
    vec4 tmp = { v[0], v[1], v[2], w };
    Matrix4MulVec(m, tmp, out);
}

#endif // PJD_MAT4_H