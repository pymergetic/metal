/** @file
  Stackless coroutines: await, sleep, gather, wait_for. (impl: efi)
**/
#include <coro/coro.h>
#include <task/task.h>
#include <mem/mem.h>
#include <time/time.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/SynchronizationLib.h>

/* ---- timers (sleep / wait_for) ---- */

typedef struct pm_metal_timer {
  struct pm_metal_timer   *next;
  uint64_t                 deadline_us;
  pm_metal_task_t         *task;
  pm_metal_coro_t         *wait_for; /* non-NULL → timeout path */
  struct pm_metal_timer  **owner_slot;
  int                      cancelled;
  int                      linked;
} pm_metal_timer_t;

STATIC pm_metal_timer_t  *mTimers;
STATIC SPIN_LOCK          mTimerLock;
STATIC int                mTimerLockReady;

STATIC
void
MetalTimerLockInit (
  VOID
  )
{
  if (!mTimerLockReady) {
    InitializeSpinLock (&mTimerLock);
    mTimerLockReady = 1;
  }
}

STATIC
void
MetalTimerUnlinkLocked (
  pm_metal_timer_t  *tm
  )
{
  pm_metal_timer_t  **pp;

  if (tm == NULL || !tm->linked) {
    return;
  }

  pp = &mTimers;
  while (*pp != NULL) {
    if (*pp == tm) {
      *pp = tm->next;
      break;
    }

    pp = &(*pp)->next;
  }

  tm->next   = NULL;
  tm->linked = 0;
}

/**
  Drop owner slot: unlink + free. Safe if poll already took the timer
  (slot is NULL).
*/
STATIC
void
MetalTimerDrop (
  pm_metal_timer_t  **slot
  )
{
  pm_metal_timer_t  *tm;

  if (slot == NULL) {
    return;
  }

  MetalTimerLockInit ();
  AcquireSpinLock (&mTimerLock);
  tm = *slot;
  if (tm != NULL) {
    *slot = NULL;
    if (tm->owner_slot == slot) {
      tm->owner_slot = NULL;
    }

    MetalTimerUnlinkLocked (tm);
  }

  ReleaseSpinLock (&mTimerLock);

  if (tm != NULL) {
    pm_metal_mem_free (tm);
  }
}

STATIC
pm_metal_timer_t *
MetalTimerArm (
  uint32_t            ms,
  pm_metal_task_t    *task,
  pm_metal_coro_t    *wait_for,
  pm_metal_timer_t  **owner_slot
  )
{
  pm_metal_timer_t  *tm;

  tm = (pm_metal_timer_t *)pm_metal_mem_alloc (
                             sizeof (*tm),
                             PM_METAL_MEM_HEAP,
                             PM_METAL_MEM_ID_NONE
                             );
  if (tm == NULL) {
    return NULL;
  }

  ZeroMem (tm, sizeof (*tm));
  tm->deadline_us = pm_metal_time_mono_us () + (uint64_t)ms * 1000u;
  tm->task        = task;
  tm->wait_for    = wait_for;
  tm->owner_slot  = owner_slot;
  if (owner_slot != NULL) {
    *owner_slot = tm;
  }

  MetalTimerLockInit ();
  AcquireSpinLock (&mTimerLock);
  tm->next   = mTimers;
  tm->linked = 1;
  mTimers    = tm;
  ReleaseSpinLock (&mTimerLock);
  return tm;
}

/* ---- sleep ---- */

typedef struct {
  pm_metal_coro_t    coro;
  uint32_t           ms;
  int                armed;
  pm_metal_timer_t  *tm;
} pm_metal_sleep_coro_t;

STATIC
void
MetalSleepRelease (
  pm_metal_coro_t  *self
  )
{
  pm_metal_sleep_coro_t  *s;

  s = (pm_metal_sleep_coro_t *)self;
  MetalTimerDrop (&s->tm);
}

STATIC
pm_metal_status_t
MetalSleepFn (
  pm_metal_coro_t  *self
  )
{
  pm_metal_sleep_coro_t  *s;

  s = (pm_metal_sleep_coro_t *)self;
  if (!s->armed) {
    if (self->owner == NULL) {
      return PM_METAL_ERROR;
    }

    if (MetalTimerArm (s->ms, self->owner, NULL, &s->tm) == NULL) {
      return PM_METAL_ERROR;
    }

    s->armed = 1;
    return PM_METAL_WAITING;
  }

  MetalTimerDrop (&s->tm);
  return PM_METAL_DONE;
}

/* ---- gather ---- */

typedef struct {
  pm_metal_coro_t    coro;
  pm_metal_task_t  **tasks;
  size_t             n;
  size_t             i;
  uint32_t           step;
} pm_metal_gather_coro_t;

STATIC
void
MetalGatherRelease (
  pm_metal_coro_t  *self
  )
{
  pm_metal_gather_coro_t  *g;
  size_t                   k;

  g = (pm_metal_gather_coro_t *)self;
  if (g->tasks != NULL) {
    for (k = 0; k < g->n; k++) {
      if (g->tasks[k] != NULL) {
        pm_metal_task_destroy (g->tasks[k]);
        g->tasks[k] = NULL;
      }
    }

    pm_metal_mem_free (g->tasks);
    g->tasks = NULL;
  }

  g->n = 0;
}

STATIC
pm_metal_status_t
MetalGatherFn (
  pm_metal_coro_t  *self
  )
{
  pm_metal_gather_coro_t  *g;
  pm_metal_status_t        st;

  g = (pm_metal_gather_coro_t *)self;

  if (g->step == 0) {
    size_t  k;

    for (k = 0; k < g->n; k++) {
      if (g->tasks[k] == NULL) {
        MetalGatherRelease (self);
        return PM_METAL_ERROR;
      }
    }

    g->step = 1;
    g->i    = 0;
  }

  while (g->i < g->n) {
    st = pm_metal_await_task (self, g->tasks[g->i]);
    if (st == PM_METAL_WAITING) {
      return PM_METAL_WAITING;
    }

    if (st != PM_METAL_DONE) {
      MetalGatherRelease (self);
      return st;
    }

    /* Child task finished; drop it now (result already consumed). */
    pm_metal_task_destroy (g->tasks[g->i]);
    g->tasks[g->i] = NULL;
    g->i++;
  }

  MetalGatherRelease (self);
  return PM_METAL_DONE;
}

/* ---- wait_for ---- */

typedef struct {
  pm_metal_coro_t    coro;
  pm_metal_task_t   *child;
  pm_metal_timer_t  *tm;
  uint32_t           ms;
  uint32_t           step;
  int                timed_out;
} pm_metal_wait_for_coro_t;

STATIC
void
MetalWaitForRelease (
  pm_metal_coro_t  *self
  )
{
  pm_metal_wait_for_coro_t  *w;

  w = (pm_metal_wait_for_coro_t *)self;
  MetalTimerDrop (&w->tm);
  if (w->child != NULL) {
    pm_metal_task_destroy (w->child);
    w->child = NULL;
  }
}

STATIC
pm_metal_status_t
MetalWaitForFn (
  pm_metal_coro_t  *self
  )
{
  pm_metal_wait_for_coro_t  *w;
  pm_metal_status_t          st;

  w = (pm_metal_wait_for_coro_t *)self;

  if (w->timed_out) {
    return PM_METAL_ERROR;
  }

  if (w->step == 0) {
    if (w->child == NULL || self->owner == NULL) {
      return PM_METAL_ERROR;
    }

    if (MetalTimerArm (w->ms, self->owner, self, &w->tm) == NULL) {
      return PM_METAL_ERROR;
    }

    w->step = 1;
  }

  st = pm_metal_await_task (self, w->child);
  if (st == PM_METAL_WAITING) {
    return PM_METAL_WAITING;
  }

  MetalTimerDrop (&w->tm);
  if (w->timed_out) {
    return PM_METAL_ERROR;
  }

  /*
    Result may point into the child task. Child is destroyed when this
    wait_for coro is closed (after the parent step has run).
  */
  self->result = pm_metal_task_result (w->child);
  return st;
}

/* ---- public ---- */

pm_metal_coro_t *
pm_metal_coro (
  pm_metal_coro_fn  fn,
  size_t            bytes
  )
{
  pm_metal_coro_t  *c;

  if (fn == NULL || bytes < sizeof (pm_metal_coro_t)) {
    return NULL;
  }

  c = (pm_metal_coro_t *)pm_metal_mem_alloc (
                           bytes,
                           PM_METAL_MEM_HEAP,
                           PM_METAL_MEM_ID_NONE
                           );
  if (c == NULL) {
    return NULL;
  }

  ZeroMem (c, bytes);
  c->fn     = fn;
  c->status = PM_METAL_PENDING;
  c->bytes  = bytes;
  return c;
}

void
pm_metal_coro_close (
  pm_metal_coro_t  *c
  )
{
  if (c == NULL) {
    return;
  }

  if (c->release != NULL) {
    c->release (c);
    c->release = NULL;
  }

  pm_metal_mem_free (c);
}

pm_metal_status_t
pm_metal_await (
  pm_metal_coro_t  *self,
  pm_metal_coro_t  *aw
  )
{
  if (self == NULL || aw == NULL) {
    return PM_METAL_ERROR;
  }

  aw->owner       = self->owner;
  aw->waiter      = self;
  self->awaiting  = aw;
  self->status    = PM_METAL_WAITING;
  return PM_METAL_WAITING;
}

pm_metal_status_t
pm_metal_coro_resume (
  pm_metal_coro_t  *c
  )
{
  pm_metal_coro_t   *leaf;
  pm_metal_status_t  st;

  if (c == NULL || c->fn == NULL) {
    return PM_METAL_ERROR;
  }

  leaf = c;
  while (leaf->awaiting != NULL) {
    leaf = leaf->awaiting;
  }

  if (leaf->status == PM_METAL_DONE
      || leaf->status == PM_METAL_ERROR
      || leaf->status == PM_METAL_CANCELLED)
  {
    return leaf->status;
  }

  st = leaf->fn (leaf);
  leaf->status = st;
  return st;
}

pm_metal_coro_t *
pm_metal_sleep (
  uint32_t  ms
  )
{
  pm_metal_sleep_coro_t  *s;

  s = (pm_metal_sleep_coro_t *)pm_metal_coro (
                                 MetalSleepFn,
                                 sizeof (*s)
                                 );
  if (s == NULL) {
    return NULL;
  }

  s->ms          = ms;
  s->coro.release = MetalSleepRelease;
  return &s->coro;
}

/* ---- yield (schedule, not time) ---- */

typedef struct {
  pm_metal_coro_t  coro;
  int              armed;
} pm_metal_yield_coro_t;

STATIC
pm_metal_status_t
MetalYieldFn (
  pm_metal_coro_t  *self
  )
{
  pm_metal_yield_coro_t  *y;
  pm_metal_task_t        *task;

  y = (pm_metal_yield_coro_t *)self;
  if (y->armed) {
    return PM_METAL_DONE;
  }

  task = self->owner;
  if (task == NULL) {
    return PM_METAL_ERROR;
  }

  /*
    Continuation first, then park. Same-CPU inbox is FIFO: anything already
    queued (and anything posted earlier in this step) runs before we resume.
    That is the fairness contract — distinct from sleep(0) via timers.
  */
  if (pm_metal_task_spawn (task, task->cpu) != 0) {
    return PM_METAL_ERROR;
  }

  y->armed = 1;
  return PM_METAL_WAITING;
}

pm_metal_coro_t *
pm_metal_yield (
  VOID
  )
{
  pm_metal_yield_coro_t  *y;

  y = (pm_metal_yield_coro_t *)pm_metal_coro (
                                 MetalYieldFn,
                                 sizeof (*y)
                                 );
  if (y == NULL) {
    return NULL;
  }

  return &y->coro;
}

pm_metal_coro_t *
pm_metal_gather (
  pm_metal_coro_t **aws,
  size_t            n
  )
{
  pm_metal_gather_coro_t  *g;
  size_t                   k;

  if (aws == NULL && n != 0) {
    return NULL;
  }

  g = (pm_metal_gather_coro_t *)pm_metal_coro (
                                  MetalGatherFn,
                                  sizeof (*g)
                                  );
  if (g == NULL) {
    return NULL;
  }

  g->coro.release = MetalGatherRelease;
  g->n            = n;
  if (n > 0) {
    g->tasks = (pm_metal_task_t **)pm_metal_mem_alloc (
                                     n * sizeof (pm_metal_task_t *),
                                     PM_METAL_MEM_HEAP,
                                     PM_METAL_MEM_ID_NONE
                                     );
    if (g->tasks == NULL) {
      pm_metal_coro_close (&g->coro);
      return NULL;
    }

    ZeroMem (g->tasks, n * sizeof (pm_metal_task_t *));
    for (k = 0; k < n; k++) {
      if (aws[k] == NULL) {
        pm_metal_coro_close (&g->coro);
        return NULL;
      }

      g->tasks[k] = pm_metal_create_task (aws[k]);
      if (g->tasks[k] == NULL) {
        pm_metal_coro_close (&g->coro);
        return NULL;
      }
    }
  }

  return &g->coro;
}

pm_metal_coro_t *
pm_metal_wait_for (
  pm_metal_coro_t  *aw,
  uint32_t          ms
  )
{
  pm_metal_wait_for_coro_t  *w;

  if (aw == NULL) {
    return NULL;
  }

  w = (pm_metal_wait_for_coro_t *)pm_metal_coro (
                                    MetalWaitForFn,
                                    sizeof (*w)
                                    );
  if (w == NULL) {
    pm_metal_coro_close (aw);
    return NULL;
  }

  w->coro.release = MetalWaitForRelease;
  w->ms           = ms;
  w->child        = pm_metal_create_task (aw);
  if (w->child == NULL) {
    pm_metal_coro_close (&w->coro);
    return NULL;
  }

  return &w->coro;
}

void
pm_metal_coro_poll_timers (
  VOID
  )
{
  uint64_t  now;

  MetalTimerLockInit ();
  now = pm_metal_time_mono_us ();

  for (;;) {
    pm_metal_timer_t   *tm;
    pm_metal_task_t    *task;
    pm_metal_coro_t    *wait_for;
    int                 cancelled;

    tm = NULL;
    AcquireSpinLock (&mTimerLock);
    {
      pm_metal_timer_t  **pp;

      pp = &mTimers;
      while (*pp != NULL) {
        if (now >= (*pp)->deadline_us) {
          tm = *pp;
          MetalTimerUnlinkLocked (tm);
          if (tm->owner_slot != NULL && *tm->owner_slot == tm) {
            *tm->owner_slot = NULL;
          }

          tm->owner_slot = NULL;
          break;
        }

        pp = &(*pp)->next;
      }
    }

    if (tm == NULL) {
      ReleaseSpinLock (&mTimerLock);
      break;
    }

    cancelled = tm->cancelled;
    task      = tm->task;
    wait_for  = tm->wait_for;
    ReleaseSpinLock (&mTimerLock);

    if (!cancelled) {
      if (wait_for != NULL) {
        pm_metal_wait_for_coro_t  *w;

        w = (pm_metal_wait_for_coro_t *)wait_for;
        w->timed_out = 1;
        if (w->child != NULL) {
          pm_metal_task_cancel (w->child);
        }
      }

      if (task != NULL) {
        (VOID)pm_metal_task_spawn (task, task->cpu);
      }
    }

    pm_metal_mem_free (tm);
  }
}
