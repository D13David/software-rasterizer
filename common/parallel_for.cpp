#include "parallel_for.h"
#include "common/common.h"

#include <malloc.h>
#include <atomic>

using atomic_int = std::atomic<int>;

typedef struct ParallelForContext
{
    PJD_ALIGN(64)  atomic_int NextIndex;
    PJD_ALIGN(64)  atomic_int WorkersRemaining;
    int             End;
    int             GrainSize;
    ParallelForFunc Func;
    void*           UserContext;
} ParallelForContext;

static void ParallelForWorker(size_t id, void* arg)
{
    ParallelForContext* context = (ParallelForContext*)arg;

    while (true)
    {
        int start = std::atomic_fetch_add(&context->NextIndex, context->GrainSize);

        if (start >= context->End) 
        {
            if (context->WorkersRemaining.fetch_sub(1) == 1) {
                free(context);
            }
            break;
        }

        int end = start + context->GrainSize;
        if (end >= context->End) {
            end = context->End;
        }

        context->Func(id, start, end, context->UserContext);
    }
}

void ParallelFor(ThreadPoolHandle pool, int begin, int end, int grainSize, ParallelForFunc func, bool syncWithTasks, void* userContext)
{
    if (pool == NULL || func == NULL || begin >= end) {
        return;
    }

    if (grainSize <= 0) {
        grainSize = 1;
    }

    int numJobs = ThreadPoolGetNumWorkers(pool);

    ParallelForContext* context = (ParallelForContext*)malloc(sizeof(ParallelForContext));
    context->NextIndex.store(begin);
    context->End = end;
    context->GrainSize = grainSize;
    context->Func = func;
    context->UserContext = userContext;
    context->WorkersRemaining.store(numJobs);

    for (int i = 0; i < numJobs; ++i) {
        ThreadPoolAddJob(pool, &ParallelForWorker, context);
    }

    if (syncWithTasks) {
        ThreadPoolWaitForJobs(pool);
    }
}
