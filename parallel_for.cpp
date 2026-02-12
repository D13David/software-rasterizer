#include "parallel_for.h"

#include <malloc.h>
#include <atomic>

using atomic_int = std::atomic<int>;

typedef struct ParallelForContext
{
    atomic_int      NextIndex;
    int             End;
    int             GrainSize;
    ParallelForFunc Func;
    void*           UserContext;
} ParallelForContext;

static void ParallelForWorker(void* arg)
{
    ParallelForContext* context = (ParallelForContext*)arg;

    while (true)
    {
        int start = std::atomic_fetch_add(&context->NextIndex, context->GrainSize);

        if (start >= context->End) {
            break;
        }

        int end = start + context->GrainSize;
        if (end >= context->End) {
            end = context->End;
        }

        context->Func(start, end, context->UserContext);
    }
}

void ParallelFor(ThreadPoolHandle pool, int begin, int end, int grainSize, ParallelForFunc func, void* userContext)
{
    if (pool == NULL || func == NULL || begin >= end) {
        return;
    }

    if (grainSize <= 0) {
        grainSize = 1;
    }

    ParallelForContext context;
    context.NextIndex.store(begin);
    context.End = end;
    context.GrainSize = grainSize;
    context.Func = func;
    context.UserContext = userContext;

    for (int i = 0; i < ThreadPoolGetNumWorkers(pool); ++i) {
        ThreadPoolAddJob(pool, &ParallelForWorker, &context);
    }

    ThreadPoolWaitForJobs(pool);
}
