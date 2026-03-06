#ifndef PJD_THREAD_POOL_H
#define PJD_THREAD_POOL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ThreadPool* ThreadPoolHandle;

enum ShutdownMode
{
    WAIT_FOR_JOBS,
    IMMEDIATE
};

ThreadPoolHandle ThreadPoolInit(int numWorkers, int maxJobs);
void ThreadPoolDestroy(ThreadPoolHandle pool, ShutdownMode mode);
void ThreadPoolWaitForJobs(ThreadPoolHandle pool);
int ThreadPoolAddJob(ThreadPoolHandle pool, void (*proc)(size_t, void*), void* arg);
int ThreadPoolGetNumWorkers(ThreadPoolHandle pool);

#ifdef __cplusplus
}
#endif

#endif // PJD_THREAD_POOL_H