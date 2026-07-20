/** @file
  Stackless coroutines / awaitables — Python asyncio-shaped (docs/COOP_MEMORY.md).

  Concrete types embed pm_metal_coro_t as the first field and allocate
  sizeof(concrete) via pm_metal_coro().
**/
#ifndef PM_METAL_CORO_H_
#define PM_METAL_CORO_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  PM_METAL_PENDING = 0,
  PM_METAL_WAITING,
  PM_METAL_DONE,
  PM_METAL_CANCELLED,
  PM_METAL_ERROR
} pm_metal_status_t;

typedef struct pm_metal_coro pm_metal_coro_t;
typedef struct pm_metal_task pm_metal_task_t;

/** One step. Cast self to the concrete type (header must be first). */
typedef pm_metal_status_t (*pm_metal_coro_fn)(
  pm_metal_coro_t  *self
  );

/**
  Awaitable frame — embed as the first field of every concrete coro:

    typedef struct {
      pm_metal_coro_t  coro;
      uint32_t         step;
      ...
    } my_coro_t;
*/
/** Optional owned-resource teardown (gather kids, wait_for child, …). */
typedef void (*pm_metal_coro_release_fn)(
  pm_metal_coro_t  *self
  );

struct pm_metal_coro {
  pm_metal_coro_fn          fn;
  pm_metal_status_t         status;
  size_t                    bytes;
  pm_metal_coro_t          *awaiting; /* nested await (NULL if none) */
  pm_metal_coro_t          *waiter;   /* parent waiting on us */
  pm_metal_task_t          *owner;    /* independent flow (Task), if any */
  void                     *result;
  pm_metal_coro_release_fn  release;  /* called from coro_close, then freed */
};

/**
  Python: c = foo()  — heap-allocate `bytes`, zero, install fn.
  `bytes` must be >= sizeof(pm_metal_coro_t).
*/
/* impl: efi */
pm_metal_coro_t *pm_metal_coro (
  pm_metal_coro_fn  fn,
  size_t            bytes
  );

/* impl: efi */
void pm_metal_coro_close (
  pm_metal_coro_t  *c
  );

/**
  Python: await aw
  Park `self` on nested awaitable `aw` (same task / flow).
  Returns PM_METAL_WAITING. On completion the child is closed (see release).

  Join a concurrent Task with pm_metal_await_task — that is the create_task
  join path (also used inside gather / wait_for).
*/
/* impl: efi */
pm_metal_status_t pm_metal_await (
  pm_metal_coro_t  *self,
  pm_metal_coro_t  *aw
  );

/**
  Drive one step at the leaf of `c`’s await chain; bubble DONE upward.
  Prefer pm_metal_task_step on a Task; this is for advanced / tests.
*/
/* impl: efi */
pm_metal_status_t pm_metal_coro_resume (
  pm_metal_coro_t  *c
  );

/**
  Python: asyncio.sleep(ms / 1000)
  Returns a new awaitable that completes after `ms` milliseconds.
*/
/* impl: efi */
pm_metal_coro_t *pm_metal_sleep (
  uint32_t  ms
  );

/**
  Python: await asyncio.sleep(0) — scheduling yield, not a timer.

  Parks the owning task and enqueues it at the tail of its current CPU
  inbox. The runloop drains other ready messages on that CPU before this
  task steps again. No TSC/timer involvement; fairness is inbox FIFO.

  Use: return pm_metal_await(self, pm_metal_yield());
*/
/* impl: efi */
pm_metal_coro_t *pm_metal_yield (
  void
  );

/**
  Python: asyncio.gather(*aws)
  Concurrently drive `n` awaitables (via create_task), then complete.
  Takes ownership of the child coro pointers (not the array).
*/
/* impl: efi */
pm_metal_coro_t *pm_metal_gather (
  pm_metal_coro_t **aws,
  size_t            n
  );

/**
  Python: asyncio.wait_for(aw, timeout)
  Await `aw` or fail with PM_METAL_ERROR after `ms` milliseconds.
  Takes ownership of `aw`.
*/
/* impl: efi */
pm_metal_coro_t *pm_metal_wait_for (
  pm_metal_coro_t  *aw,
  uint32_t          ms
  );

/** Wake due sleep/wait_for timers (called from idle runloop). */
/* impl: efi */
void pm_metal_coro_poll_timers (
  void
  );

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_CORO_H_ */
