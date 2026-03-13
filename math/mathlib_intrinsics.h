#ifndef PJD_MATHLIB_INTRINSICS_H
#define PJD_MATHLIB_INTRINSICS_H

#if defined(__AVX__)
#   include <immintrin.h>
#   ifndef __SSE2__
#       define __SSE2__
#   endif
#endif

#if defined(__SSE2__)
#   include <emmintrin.h>
#endif

#endif // PJD_MATHLIB_INTRINSICS_H