#ifndef PJD_COMMON_H
#define PJD_COMMON_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#if defined(_MSC_VER)
#   define PJD_INLINE __forceinline
#else
#   error "compiler not supported"
#endif

#if defined(_MSC_VER)
#   define PJD_ALIGN(x) __declspec(align(x))
#else
#   error "compiler not supported"
#endif

PJD_INLINE void Trace(const char* fmt, ...)
{
    char buffer[1024 * 8];
    va_list args;
    va_start(args, fmt);
    vsprintf_s(buffer, fmt, args);
    va_end(args);

    OutputDebugStringA(buffer);
}

#endif // PJD_COMMON_H