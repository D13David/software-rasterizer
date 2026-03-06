#ifndef PJD_COMMON_H
#define PJD_COMMON_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#ifndef PJD_PROFILING_BUILD
#   define PJD_USE_RENDER_STATS 1
#endif

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

#if defined(_MSC_VER)
#   define PJD_THREAD_LOCAL __declspec(thread)
#else
#   error "compiler not supported"
#endif

#define CONCAT_(a, b) a##b
#define CONCAT(a, b) CONCAT_(a, b)

template<typename F>
struct Scoped { F f; ~Scoped() { f(); } };

//template<typename F> Scoped<F> scoped(F f) { return Scoped<F>{f}; }

PJD_INLINE const char* Format(const char* fmt, ...)
{
    thread_local char buffer[8192];
    va_list args;
    va_start(args, fmt);
    vsprintf_s(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}

#define Trace(fmt, ...) OutputDebugStringA(Format(fmt, __VA_ARGS__))

#endif // PJD_COMMON_H