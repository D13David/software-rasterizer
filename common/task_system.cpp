#include "task_system.h"
#include "common/common.h"
#include "common/thread_pool.h"

static void TaskAddWaiter(Task* dep, Task* waiter)
{
    Task* head;

    do
    {
        head = atomic_load(&dep->waiters);
        waiter->next_waiter = head;
    } while (!atomic_compare_exchange_weak(&dep->waiters, &head, waiter));
}

static void TaskSetParent(Task* child, Task* parent)
{
    child->parent = parent;
    atomic_fetch_add(&parent->pending, 1);
}

static void TaskFinish(Task* task, ThreadPoolHandle pool)
{
    atomic_store(&task->completed, true);

    Task* waiter = atomic_exchange(&task->waiters, NULL);

    while (waiter)
    {
        Task* next = waiter->next_waiter;

        if (atomic_fetch_sub(&waiter->deps, 1) == 1) {
            ThreadPoolAddJob(pool, (void(*)(size_t, void*))waiter->func, waiter->arg);
        }

        waiter = next;
    }

    if (task->parent)
    {
        Task* p = task->parent;

        if (atomic_fetch_sub(&p->pending, 1) == 1) {
            TaskFinish(p, pool);
        }
    }
}

void TaskInit(Task* task, void (*func)(Task*, void*), void* arg) 
{
    task->func = func;
    task->arg = arg;

    atomic_store(&task->deps, 0);
    atomic_store(&task->pending, 0);
    atomic_store(&task->completed, false);

    task->parent = NULL;

    atomic_store(&task->waiters, NULL);
    task->next_waiter = NULL;
}

void TaskDependsOn(Task* task, Task* dep, ThreadPoolHandle pool)
{
    atomic_fetch_add(&task->deps, 1);

    TaskAddWaiter(dep, task);

    if (atomic_load(&dep->completed))
    {
        if (atomic_fetch_sub(&task->deps, 1) == 1) {
            ThreadPoolAddJob(pool, (void(*)(size_t, void*))task->func, task->arg);
        }
    }
}

void TaskSubmit(Task* task, ThreadPoolHandle pool)
{
    if (atomic_load(&task->deps) == 0)
    {
        ThreadPoolAddJob(pool, (void(*)(size_t, void*))task->func, task->arg);
    }
}

/*
* typedef struct {
    size_t start, end;
    void (*func)(size_t, void*);
    void* arg;
    Task task;
    ThreadPoolHandle pool;
} PFChunk;

void parallel_for_task(Task* t, void* arg) {
    PFChunk* c = (PFChunk*)arg;

    for (size_t i = c->start; i < c->end; i++) {
        c->func(i, c->arg);
    }

    task_finish(t, c->pool);
}

void parallel_for(Task* parent,
    ThreadPoolHandle pool,
    size_t count,
    size_t chunkSize,
    void (*func)(size_t, void*),
    void* arg,
    PFChunk* arena,
    size_t maxChunks)
{
    size_t num = (count + chunkSize - 1) / chunkSize;
    if (num > maxChunks) num = maxChunks;

    for (size_t i = 0; i < num; i++) {
        PFChunk* c = &arena[i];

        size_t start = i * chunkSize;
        size_t end = start + chunkSize;
        if (end > count) end = count;

        c->start = start;
        c->end = end;
        c->func = func;
        c->arg = arg;
        c->pool = pool;

        task_init(&c->task, parallel_for_task, c);
        task_set_parent(&c->task, parent);

        ThreadPoolAddJob(pool, (void(*)(size_t, void*))parallel_for_task, c);
    }
}
*/