#ifndef PJD_MATHLIB_COMMON_H
#define PJD_MATHLIB_COMMON_H

#include "mathlib_types.h"
#include "vec3.h"

#define PJD_PI      3.1415926535897932f
#define PJD_PI_2    1.5707963267948966f
#define PJD_PI_4    0.7853981633974483f

#define DEG2RAD(d)  (((d)*PJD_PI) / 180.0f)
#define RAD2DEG(d)  (((d)*180.0f) / PJD_PI

#define max(a,b)    (((a) > (b)) ? (a) : (b))
#define min(a,b)    (((a) < (b)) ? (a) : (b))
#define max3(a,b,c) (((a) > (b) ? (a) : (b)) > (c) ? ((a) > (b) ? (a) : (b)) : (c))
#define min3(a,b,c) (((a) < (b) ? (a) : (b)) < (c) ? ((a) < (b) ? (a) : (b)) : (c))

PJD_INLINE int Align(int value, int alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

PJD_INLINE void SinCos(float value, float* s, float* c)
{
    assert(s != NULL && c != NULL);
    *s = sinf(value);
    *c = cosf(value);
}

PJD_INLINE float Clamp(float value, float min, float max)
{
    return min(max(value, min), max);
}

PJD_INLINE float Saturate(float value)
{
    return Clamp(value, 0.0f, 1.0f);
}

PJD_INLINE float Lerp(float a, float b, float t) 
{ 
    return a * (1.0f - t) + b * t; 
}

PJD_INLINE void LerpVec2(const vec2 a, const vec2 b, float t, vec2 out)
{
    out[0] = Lerp(a[0], b[0], t);
    out[1] = Lerp(a[1], b[1], t);
}

PJD_INLINE void LerpVec3(const vec3 a, const vec3 b, float t, vec3 out)
{
    out[0] = Lerp(a[0], b[0], t);
    out[1] = Lerp(a[1], b[1], t);
    out[2] = Lerp(a[2], b[2], t);
}

PJD_INLINE void LerpVec4(const vec4 a, const vec4 b, float t, vec4 out)
{
    out[0] = Lerp(a[0], b[0], t);
    out[1] = Lerp(a[1], b[1], t);
    out[2] = Lerp(a[2], b[2], t);
    out[3] = Lerp(a[3], b[3], t);
}

PJD_INLINE float Vec4Dot(const vec4 a, const vec4 b)
{
    return (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
}

PJD_INLINE void Vec2Copy(const vec2 src, vec2 dst)
{
    memcpy(dst, src, sizeof(vec2));
}

PJD_INLINE void Vec3Copy(const vec3 src, vec3 dst)
{
    memcpy(dst, src, sizeof(vec3));
}

PJD_INLINE void Vec4Copy(const vec4 src, vec4 dst)
{
    memcpy(dst, src, sizeof(vec4));
}

PJD_INLINE float Log2Fast(float x)
{
#if 0
    return log2f(x);
#else
    union { float f; uint32_t i; } vx = { x };
    float y = (float)vx.i;
    y *= 1.1920928955078125e-7f;
    return y - 127.0f;
#endif
}

PJD_INLINE int RandomRange(int min, int max)
{
    return rand() % (max - min + 1) + min;
}

PJD_INLINE int Encode6BitMorton(int x, int y)
{
    auto split1by1 = [](int value)
        {
            value &= 0x3F;
            value = (value | (value << 4)) & 0x0F0F;
            value = (value | (value << 2)) & 0x3333;
            value = (value | (value << 1)) & 0x5555;
            return value;
        };
    return split1by1(x) | (split1by1(y) << 1);
}

PJD_INLINE void Decode6BitMorton(int code, int* x, int* y)
{
    auto compact1by1 = [](int value)
        {
            value &= 0x555;
            value = (value ^ (value >> 1)) & 0x333;
            value = (value ^ (value >> 2)) & 0x0F0F;
            value = (value ^ (value >> 4)) & 0x3F;
            return value;
        };
    *x = compact1by1(code);
    *y = compact1by1(code >> 1);
}

//=============================================================================
// Affine Transformation Functionality
//=============================================================================

PJD_INLINE void CreateMatrixTransform(float x, float y, float z, mat4 out)
{
    out[0][0] = 1; out[0][1] = 0; out[0][2] = 0; out[0][3] = x;
    out[1][0] = 0; out[1][1] = 1; out[1][2] = 0; out[1][3] = y;
    out[2][0] = 0; out[2][1] = 0; out[2][2] = 1; out[2][3] = z;
    out[3][0] = 0; out[3][1] = 0; out[3][2] = 0; out[3][3] = 1;
}

PJD_INLINE void CreateMatrixRotateX(float angle, mat4 out)
{
    float s, c;
    SinCos(angle, &s, &c);
    out[0][0] = 1; out[0][1] = 0; out[0][2] = 0; out[0][3] = 0;
    out[1][0] = 0; out[1][1] = c; out[1][2] = -s; out[1][3] = 0;
    out[2][0] = 0; out[2][1] = s; out[2][2] = c; out[2][3] = 0;
    out[3][0] = 0; out[3][1] = 0; out[3][2] = 0; out[3][3] = 1;
}

PJD_INLINE void CreateMatrixRotateY(float angle, mat4 out)
{
    float s, c;
    SinCos(angle, &s, &c);
    out[0][0] = c; out[0][1] = 0; out[0][2] = s; out[0][3] = 0;
    out[1][0] = 0; out[1][1] = 1; out[1][2] = 0; out[1][3] = 0;
    out[2][0] = -s; out[2][1] = 0; out[2][2] = c; out[2][3] = 0;
    out[3][0] = 0; out[3][1] = 0; out[3][2] = 0; out[3][3] = 1;
}

PJD_INLINE void CreateMatrixRotateZ(float angle, mat4 out)
{
    float s, c;
    SinCos(angle, &s, &c);
    out[0][0] = c; out[0][1] = -s; out[0][2] = 0; out[0][3] = 0;
    out[1][0] = s; out[1][1] = c; out[1][2] = 0; out[1][3] = 0;
    out[2][0] = 0; out[2][1] = 0; out[2][2] = 1; out[2][3] = 0;
    out[3][0] = 0; out[3][1] = 0; out[3][2] = 0; out[3][3] = 1;
}

PJD_INLINE void CreateMatrixLookAt(vec3 eye, vec3 at, vec3 up, mat4 out)
{
    vec3 xaxis, yaxis, zaxis;
    Vec3Sub(at, eye, zaxis);
    Vec3Cross(up, zaxis, xaxis), Vec3NormalizeSelf(xaxis);
    Vec3Cross(zaxis, xaxis, yaxis);

    float tx = -Vec3Dot(xaxis, eye);
    float ty = -Vec3Dot(yaxis, eye);
    float tz = -Vec3Dot(zaxis, eye);

    out[0][0] = xaxis[0]; out[0][1] = xaxis[1]; out[0][2] = xaxis[2]; out[0][3] = tx;
    out[1][0] = yaxis[0]; out[1][1] = yaxis[1]; out[1][2] = yaxis[2]; out[1][3] = ty;
    out[2][0] = zaxis[0]; out[2][1] = zaxis[1]; out[2][2] = zaxis[2]; out[2][3] = tz;
    out[3][0] = 0;        out[3][1] = 0;        out[3][2] = 0;        out[3][3] = 1;
}

//=============================================================================
// Camera/Viewport Functionality
//=============================================================================
PJD_INLINE void CreateMatrixPerspectiveFovLH(float fovy, float aspect, float zn, float zf, mat4 out)
{
    float sy = 1.0f / tanf(DEG2RAD(fovy * 0.5f));
    float sx = sy / aspect;
    float invNearFar = 1.0f / (zf - zn);

    out[0][0] = sx; out[0][1] = 0;  out[0][2] = 0;               out[0][3] = 0;
    out[1][0] = 0;  out[1][1] = sy; out[1][2] = 0;               out[1][3] = 0;
    out[2][0] = 0;  out[2][1] = 0;  out[2][2] = zf * invNearFar; out[2][3] = -zn * zf * invNearFar;
    out[3][0] = 0;  out[3][1] = 0;  out[3][2] = 1;               out[3][3] = 0;
}

PJD_INLINE void CreateMatrixPerspectiveLH(float w, float h, float zn, float zf, mat4 out)
{
    float sx = 2 * zn / w;
    float sy = 2 * zn / h;
    float invNearFar = 1.0f / (zf - zn);

    out[0][0] = sx; out[0][1] = 0;  out[0][2] = 0;               out[0][3] = 0;
    out[1][0] = 0;  out[1][1] = sy; out[1][2] = 0;               out[1][3] = 0;
    out[2][0] = 0;  out[2][1] = 0;  out[2][2] = zf * invNearFar; out[2][3] = -zn * zf * invNearFar;
    out[3][0] = 0;  out[3][1] = 0;  out[3][2] = 1;               out[3][3] = 0;
}

PJD_INLINE void CreateMatrixOrthoLH(float w, float h, float zn, float zf, mat4 out)
{
    float sx = 2 / w;
    float sy = 2 / h;
    float invNearFar = 1.0f / (zf - zn);

    out[0][0] = sx; out[0][1] = 0;  out[0][2] = 0;          out[0][3] = 0;
    out[1][0] = 0;  out[1][1] = sy; out[1][2] = 0;          out[1][3] = 0;
    out[2][0] = 0;  out[2][1] = 0;  out[2][2] = invNearFar; out[2][3] = -zn * invNearFar;
    out[3][0] = 0;  out[3][1] = 0;  out[3][2] = 0;          out[3][3] = 1;
}

PJD_INLINE void ClipToScreen(const vec4 clip, int screenWidth, int screenHeight, vec3 out)
{
    float invW = 1.0f / clip[3];
    float x = clip[0] * invW;
    float y = clip[1] * invW;
    float z = clip[2] * invW;

    out[0] = (x + 1) * 0.5f * screenWidth;
    out[1] = (1 - (y + 1) * 0.5f) * screenHeight;
    out[2] = z; // range [0,1]
    out[3] = invW;
}

#endif // PJD_MATHLIB_COMMON_H