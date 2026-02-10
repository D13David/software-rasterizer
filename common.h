#ifndef PJD_COMMON_H
#define PJD_COMMON_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>

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

#endif // PJD_COMMON_H