/** @file
  Tasks — independent flows wrapping a root coro (asyncio.Task).

  Python map:
    create_task / await task / Task.result / cancel / asyncio.run
**/
#ifndef PM_METAL_RUNTIME_TASK_TASK_H_
#define PM_METAL_RUNTIME_TASK_TASK_H_

#include <runtime/coro/coro.h>

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
  /*
   * Queue-safe lifetime (METAL-002/003): owner holds 1; each inbox TASK
   * message and each armed timer holds 1. Destroy sets doomed, drains
   * outstanding refs, then frees. Never free while refs > 1.
   */
  volatile uint32_t  refs;
  volatile uint32_t  doomed;
};

/**
  Take ownership of `coro`; do not schedule. Caller must
  pm_metal_task_spawn.
*/
/* impl: efi|bios */
pm_metal_task_t *pm_metal_task_new (
  pm_metal_coro_t  *coro
  );

/**
  Python: asyncio.create_task(coro)
  task_new + spawn on a round-robin CPU (no preferred CPU).
  Migrate / pin with pm_metal_task_spawn when you mean it.
*/
/* impl: efi|bios */
pm_metal_task_t *pm_metal_create_task (
  pm_metal_coro_t  *coro
  );

/* impl: efi|bios */
void pm_metal_task_destroy (
  pm_metal_task_t  *task
  );

/**
  Retain/release for inbox cookies and timer arms. Release never frees;
  only pm_metal_task_destroy frees after refs drain to the owner hold.
*/
/* impl: efi|bios */
void pm_metal_task_ref (
  pm_metal_task_t  *task
  );

/* impl: efi|bios */
void pm_metal_task_unref (
  pm_metal_task_t  *task
  );

/**
  Post TASK to `cpu`’s inbox (retains task for the queued message).
  When a session affinity CPU is set (async guest), create_task pins
  there; cross-CPU migrate via spawn is still allowed for non-doomed tasks.
*/
/* impl: efi|bios */
int pm_metal_task_spawn (
  pm_metal_task_t  *task,
  unsigned          cpu
  );

/**
  While set, pm_metal_create_task places work on `cpu` instead of RR.
  Used to pin a guest async session to one runner (METAL-004).
  Pass width 0 / call clear to restore round-robin.
*/
/* impl: efi|bios */
void pm_metal_task_affinity_set (
  unsigned  cpu
  );

/* impl: efi|bios */
void pm_metal_task_affinity_clear (
  void
  );

/* impl: efi|bios */
int pm_metal_task_affinity_get (
  unsigned  *cpu_out
  );

/** Drive root → leaf until park (WAITING) or terminal status. */
/* impl: efi|bios */
pm_metal_status_t pm_metal_task_step (
  pm_metal_task_t  *task
  );

/**
  Python: await task
  Returns WAITING if parked on `task`; DONE if `task` already finished
  (caller should continue without returning DONE to the runner).
*/
/* impl: efi|bios */
pm_metal_status_t pm_metal_await_task (
  pm_metal_coro_t  *self,
  pm_metal_task_t  *task
  );

/* impl: efi|bios */
pm_metal_status_t pm_metal_task_status (
  pm_metal_task_t  *task
  );

/* impl: efi|bios */
void *pm_metal_task_result (
  pm_metal_task_t  *task
  );

/* impl: efi|bios */
void pm_metal_task_cancel (
  pm_metal_task_t  *task
  );

/**
  Python: asyncio.run(main())
  create_task(main), enter runloop on `cpu` until main completes (STOP).
  Returns 0 on DONE, -1 otherwise. Runloop must already be inited.
*/
/* impl: efi|bios */
int pm_metal_task_run (
  pm_metal_coro_t  *main,
  unsigned          cpu
  );

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_RUNTIME_TASK_TASK_H_ */
