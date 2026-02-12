#ifndef PJD_PARALLEL_FOR_H
#define PJD_PARALLEL_FOR_H

#include "thread_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ParallelForFunc)(int start, int end, void* context);

void ParallelFor(ThreadPoolHandle pool, int begin, int end, int grainSize, ParallelForFunc func, void* userContext);

#ifdef __cplusplus
}
#endif

#endif // PJD_PARALLEL_FOR_H