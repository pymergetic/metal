/* pymergetic.metal.async — stackless coro/task + lock-free ready ring.
 * Park = return WAITING; frame on util.mem. Not Asyncify. */
#ifndef PYMERGETIC_METAL_ASYNC_TYPES_H
#define PYMERGETIC_METAL_ASYNC_TYPES_H

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"
#include "pymergetic/util/lock/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_METAL_ASYNC_PENDING = 0,
    PM_METAL_ASYNC_WAITING = 1,
    PM_METAL_ASYNC_DONE = 2,
    PM_METAL_ASYNC_CANCELLED = 3,
    PM_METAL_ASYNC_ERROR = 4,
} pm_metal_async_status_t;

typedef struct pm_metal_async_coro pm_metal_async_coro_t;
typedef struct pm_metal_async_task pm_metal_async_task_t;
typedef struct pm_metal_async_mutex pm_metal_async_mutex_t;

typedef pm_metal_async_status_t (*pm_metal_async_step_fn)(pm_metal_async_coro_t *self);

struct pm_metal_async_coro {
    pm_metal_async_step_fn step;
    pm_metal_async_coro_t *awaiting; /* exclusive 0..1 child; not a ring entry */
    pm_metal_async_coro_t *waiter;   /* parent */
    pm_metal_async_task_t *task;     /* outer scheduled unit (set on create_task) */
    uint32_t status;
    uint32_t vm_only; /* 1 = step() re-enters the bytecode VM; stepped under the VM lock */
};

struct pm_metal_async_task {
    pm_metal_async_coro_t *root;
    uint32_t on_c_stack; /* 1 while nested run_until owns this task's C frame */
    uint32_t running;    /* CAS: one runner steps a task at a time */
    uint32_t pid;        /* 0 = not a process (no human intent / id) */
    pm_metal_async_task_t *mutex_next; /* intrusive FIFO link while parked on a mutex */
};

/* Async-aware mutex: CAS owner, park on contention, wake on release.
 * One mechanism shared by mutex, sem, rwlock, cond — only the predicate
 * around the owner field differs. Never a spin; held across a yield.
 *
 * owner: NULL when unlocked, task pointer when held. CAS directly on this
 * pointer — no separate lock bit. fifo_lock is a raw spinlock that protects
 * only the waiters_head/tail manipulation (nanoseconds, unparkable context). */

struct pm_metal_async_mutex {
    pm_metal_async_task_t *owner; /* NULL when unlocked; CAS to claim */
    pm_metal_async_task_t *waiters_head;
    pm_metal_async_task_t *waiters_tail;
    pm_util_lock_t fifo_lock; /* protects waiters head/tail only */
    uint32_t _pad;
};

int32_t pm_metal_async_ready(void);
uint32_t pm_metal_async_n_runners(void);
const char *pm_metal_async_runner_kind(void);
uint32_t pm_metal_async_process_id(void);

/* Mark a coro as re-entering the bytecode VM: any runner core may step it,
 * serialized by the VM lock — not restricted to a single boot-thread slot. */
void pm_metal_async_coro_set_vm_only(pm_metal_async_coro_t *coro);

/* Async mutex: park-on-contention, never spin; same cast as sem/rwlock/cond. */
void pm_metal_async_mutex_init(pm_metal_async_mutex_t *m);
pm_metal_async_status_t pm_metal_async_mutex_try_acquire(pm_metal_async_mutex_t *m, pm_metal_async_coro_t *self);
void pm_metal_async_mutex_release(pm_metal_async_mutex_t *m);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_ASYNC_TYPES_H */
