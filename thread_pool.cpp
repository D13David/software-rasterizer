#include "thread_pool.h"
#include "common.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <malloc.h>

typedef struct Job
{
    void (*Proc)(void*);
    void* Arg;
} Job;

typedef struct ThreadPool
{
    HANDLE*     Workers;
    int         NumWorkers;
    bool        StopRequested;
    bool        FinishRemainingJobs;
    int         WorkersActive;

    // job queue
    void*       Buffer;
    int         Capacity;
    int         Head;
    int         Tail;
    int         Size;

    CRITICAL_SECTION QueueLock;
    CONDITION_VARIABLE WorkAvailable;
    CONDITION_VARIABLE WorkFinished;
} ThreadPool;

static DWORD WINAPI ThreadProc(LPVOID);

ThreadPoolHandle ThreadPoolInit(int numWorkers, int maxJobs)
{
    int threadsCreated = 0;

    if (numWorkers <= 0 || maxJobs <= 0) {
        return NULL;
    }

    // create thread pool
    ThreadPool* pool = (ThreadPool*)calloc(1, sizeof(ThreadPool));
    if (pool == NULL) {
        goto failed;
    }
    pool->NumWorkers = numWorkers;
    pool->StopRequested = false;
    pool->FinishRemainingJobs = true;
    pool->WorkersActive = 0;

    // initialize worker threads
    pool->Workers = (HANDLE*)calloc(numWorkers, sizeof(HANDLE));
    if (pool->Workers == NULL) {
        goto failed;
    }

    // initialize job queue
    pool->Buffer = malloc(sizeof(Job) * maxJobs);
    if (!pool->Buffer) {
        goto failed;
    }

    pool->Capacity = maxJobs;
    pool->Head = 0;
    pool->Tail = 0;
    pool->Size = 0;

    InitializeCriticalSection(&pool->QueueLock);
    InitializeConditionVariable(&pool->WorkAvailable);
    InitializeConditionVariable(&pool->WorkFinished);

    for (int i = 0; i < numWorkers; ++i) 
    {
        pool->Workers[i] = CreateThread(NULL, 0, &ThreadProc, pool, CREATE_SUSPENDED, NULL);
        if (!pool->Workers[i]) {
            break;
        }
#if _DEBUG
        WCHAR threadName[16];
        swprintf_s(threadName, L"worker-%d", i);
        SetThreadDescription(pool->Workers[i], threadName);
#endif // _DEBUG

        ++threadsCreated;
    }

    if (threadsCreated != numWorkers)
    {
        for (int i = 0; i < threadsCreated; ++i) {
            CloseHandle(pool->Workers[i]);
        }

        goto failed;
    }

    for (int i = 0; i < numWorkers; ++i) {
        ResumeThread(pool->Workers[i]);
    }

    return pool;

failed:
    if (pool != NULL)
    {
        if (pool->Workers) free(pool->Workers);
        if (pool->Buffer) free(pool->Buffer);
        free(pool);
    }

    return NULL;
}

void ThreadPoolDestroy(ThreadPoolHandle pool, ShutdownMode mode)
{
    if (pool == NULL) {
        return;
    }

    // request threads to stop and wake all of the workers
    EnterCriticalSection(&pool->QueueLock);
    pool->StopRequested = true;
    pool->FinishRemainingJobs = mode == ShutdownMode::WAIT_FOR_JOBS;
    WakeAllConditionVariable(&pool->WorkAvailable);
    LeaveCriticalSection(&pool->QueueLock);

    // sync with all workers
    for (int i = 0; i < pool->NumWorkers; ++i)
    {
        WaitForSingleObject(pool->Workers[i], INFINITE);
        CloseHandle(pool->Workers[i]);
    }

    DeleteCriticalSection(&pool->QueueLock);
    free(pool->Workers);
    free(pool->Buffer);
    free(pool);
}

void ThreadPoolWaitForJobs(ThreadPoolHandle pool)
{
    if (pool == NULL) {
        return;
    }

    EnterCriticalSection(&pool->QueueLock);
    
    assert(!pool->StopRequested);
    while (pool->Size > 0 || pool->WorkersActive > 0) {
        SleepConditionVariableCS(&pool->WorkFinished, &pool->QueueLock, INFINITE);
    }

    LeaveCriticalSection(&pool->QueueLock);
}

int ThreadPoolAddJob(ThreadPoolHandle pool, void (*proc)(void*), void* arg)
{
    if (pool == NULL || proc == NULL) {
        return 0;
    }

    EnterCriticalSection(&pool->QueueLock);

    // job queue is full, can't add another job
    if (pool->StopRequested || pool->Size == pool->Capacity) 
    {
        LeaveCriticalSection(&pool->QueueLock);
        return 0;
    }

    *((Job*)pool->Buffer + pool->Tail) = { .Proc = proc, .Arg = arg };
    pool->Tail = (pool->Tail + 1) % pool->Capacity;
    pool->Size = pool->Size + 1;

    WakeConditionVariable(&pool->WorkAvailable);
    LeaveCriticalSection(&pool->QueueLock);

    return 1;
}

int ThreadPoolGetNumWorkers(ThreadPoolHandle pool)
{
    return pool->NumWorkers;
}

static DWORD WINAPI ThreadProc(LPVOID arg)
{
    ThreadPoolHandle pool = (ThreadPoolHandle)arg;

    while (true)
    {
        EnterCriticalSection(&pool->QueueLock);

        // sleep until we get work
        while (pool->Size == 0 && !pool->StopRequested) {
            SleepConditionVariableCS(&pool->WorkAvailable, &pool->QueueLock, INFINITE);
        }

        // pool wants to tear down... so we leave
        if (pool->StopRequested && (!pool->FinishRemainingJobs || pool->Size == 0))
        {
            LeaveCriticalSection(&pool->QueueLock);
            break;
        }

        // dequeue next job
        Job job = *((Job*)pool->Buffer + pool->Head);
        pool->Head = (pool->Head + 1) % pool->Capacity;
        pool->Size = pool->Size - 1;

        pool->WorkersActive = pool->WorkersActive + 1;

        LeaveCriticalSection(&pool->QueueLock);

        job.Proc(job.Arg);

        EnterCriticalSection(&pool->QueueLock);

        pool->WorkersActive = pool->WorkersActive - 1;

        // wake up threads which wait for job to finish
        if (pool->Size == 0 && pool->WorkersActive == 0) {
            WakeAllConditionVariable(&pool->WorkFinished);
        }

        LeaveCriticalSection(&pool->QueueLock);
    }

    return 0;
}