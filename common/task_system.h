#ifndef PJD_TASK_SYSTEM_H
#define PJD_TASK_SYSTEM_H

#include "common/thread_pool.h"

#include <atomic>

using atomic_int = std::atomic<int>;
using atomic_ptr = std::atomic<struct Task*>;
using atomic_bool = std::atomic<bool>;

typedef struct Task
{
    alignas(std::max_align_t) unsigned char storage[64];

    void (*func)(Task*, void*);
    void* arg;

    atomic_int deps;
    atomic_int pending;

    atomic_bool completed;

    Task* parent;

    atomic_ptr waiters;
    Task* next_waiter;
} Task;


#ifdef __cplusplus
extern "C" {
#endif

void TaskInit(Task* task, void (*func)(Task*, void*), void* arg);
void TaskDependsOn(Task* task, Task* dep, ThreadPoolHandle pool);
void TaskSubmit(Task* task, ThreadPoolHandle pool);

#ifdef __cplusplus
}
#endif

template <typename F>
void TaskInit(Task* task, F&& f) {
    using Fn = std::decay_t<F>;

    static_assert(sizeof(Fn) <= sizeof(task->storage),
        "Lambda too large to fit in Task storage");

    // Placement new inside Task storage
    Fn* fn = new (task->storage) Fn(std::forward<F>(f));

    task->func = [](Task* t, void*) {
        Fn* fn = reinterpret_cast<Fn*>(t->storage);
        (*fn)();
        fn->~Fn(); // destroy in-place
        };
    task->arg = nullptr; // not needed since storage holds lambda
}

#endif // PJD_TASK_SYSTEM_H