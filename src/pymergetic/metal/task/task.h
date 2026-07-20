/** @file
  Tasks — independent flows wrapping a root coro (asyncio.Task).

  Python map:
    create_task / await task / Task.result / cancel / asyncio.run
**/
#ifndef PM_METAL_TASK_H_
#define PM_METAL_TASK_H_

#include <coro/coro.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
  Independent schedulable flow. Not embedded in concrete coros —
  coroutines nest via pm_metal_await; concurrency uses create_task.
*/
struct pm_metal_task {
  pm_metal_coro_t   *coro;       /* root */
  pm_metal_status_t  status;
  void              *result;
  pm_metal_coro_t   *waiter;     /* coro awaiting this task (0..1) */
  unsigned           cpu;        /* last / preferred wake CPU */
  int                cancelled;
  int                stop_on_done; /* post STOP when complete (task_run) */
  /* MP: at most one looper steps a task; extra wakes set pending_wake. */
  volatile uint32_t  busy;
  volatile uint32_t  pending_wake;
};

/**
  Take ownership of `coro`; do not schedule. Caller must
  pm_metal_task_spawn.
*/
/* impl: efi */
pm_metal_task_t *pm_metal_task_new (
  pm_metal_coro_t  *coro
  );

/**
  Python: asyncio.create_task(coro)
  task_new + spawn on a round-robin CPU (no preferred CPU).
  Migrate / pin with pm_metal_task_spawn when you mean it.
*/
/* impl: efi */
pm_metal_task_t *pm_metal_create_task (
  pm_metal_coro_t  *coro
  );

/* impl: efi */
void pm_metal_task_destroy (
  pm_metal_task_t  *task
  );

/**
  Post TASK to `cpu`’s inbox. Tasks are not CPU-affine: any looper that
  receives the pointer may step it.
*/
/* impl: efi */
int pm_metal_task_spawn (
  pm_metal_task_t  *task,
  unsigned          cpu
  );

/** Drive root → leaf until park (WAITING) or terminal status. */
/* impl: efi */
pm_metal_status_t pm_metal_task_step (
  pm_metal_task_t  *task
  );

/**
  Python: await task
  Returns WAITING if parked on `task`; DONE if `task` already finished
  (caller should continue without returning DONE to the runner).
*/
/* impl: efi */
pm_metal_status_t pm_metal_await_task (
  pm_metal_coro_t  *self,
  pm_metal_task_t  *task
  );

/* impl: efi */
pm_metal_status_t pm_metal_task_status (
  pm_metal_task_t  *task
  );

/* impl: efi */
void *pm_metal_task_result (
  pm_metal_task_t  *task
  );

/* impl: efi */
void pm_metal_task_cancel (
  pm_metal_task_t  *task
  );

/**
  Python: asyncio.run(main())
  create_task(main), enter runloop on `cpu` until main completes (STOP).
  Returns 0 on DONE, -1 otherwise. Runloop must already be inited.
*/
/* impl: efi */
int pm_metal_task_run (
  pm_metal_coro_t  *main,
  unsigned          cpu
  );

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_TASK_H_ */
