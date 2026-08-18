/* pymergetic.metal.async — stackless coro/task + lock-free ready ring.
 * Park = return WAITING; frame on util.mem. Not Asyncify. */
#ifndef PYMERGETIC_METAL_ASYNC_TYPES_H
#define PYMERGETIC_METAL_ASYNC_TYPES_H

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

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

typedef pm_metal_async_status_t (*pm_metal_async_step_fn)(pm_metal_async_coro_t *self);

struct pm_metal_async_coro {
    pm_metal_async_step_fn step;
    pm_metal_async_coro_t *awaiting; /* exclusive 0..1 child; not a ring entry */
    pm_metal_async_coro_t *waiter;   /* parent */
    pm_metal_async_task_t *task;     /* outer scheduled unit (set on create_task) */
    uint32_t status;
    uint32_t vm_only; /* 1 = step() re-enters the bytecode VM; only the VM owner may run it */
};

struct pm_metal_async_task {
    pm_metal_async_coro_t *root;
    uint32_t on_c_stack; /* 1 while nested run_until owns this task's C frame */
    uint32_t running;    /* CAS: one runner steps a task at a time */
    uint32_t pid;        /* 0 = not a process (no human intent / id) */
};

int32_t pm_metal_async_ready(void);
uint32_t pm_metal_async_n_runners(void);
const char *pm_metal_async_runner_kind(void);
uint32_t pm_metal_async_process_id(void);

/* Mark a coro as re-entering the bytecode VM: only the thread that owns the VM
 * (the boot/main thread) may step it. Background SMP runners re-queue instead. */
void pm_metal_async_coro_set_vm_only(pm_metal_async_coro_t *coro);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_ASYNC_TYPES_H */
