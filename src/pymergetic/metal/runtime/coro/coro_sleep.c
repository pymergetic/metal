/** @file
  Coop sleep (timer) + scheduling yield (inbox fairness).
**/
#include "coro_internal.h"

#include <stddef.h>

#include <runtime/time/time.h>

typedef struct {
  pm_metal_coro_t   coro;
  uint64_t          us;          /* relative; used if !absolute */
  uint64_t          deadline_us; /* absolute if absolute!=0 */
  int               absolute;
  int               armed;
  pm_metal_timer_t *tm;
} pm_metal_sleep_coro_t;

typedef struct {
  pm_metal_coro_t coro;
  int             armed;
} pm_metal_yield_coro_t;

static void MetalSleepRelease(pm_metal_coro_t *self)
{
  pm_metal_sleep_coro_t *s;

  s = (pm_metal_sleep_coro_t *)self;
  MetalTimerDrop(&s->tm);
}

static pm_metal_status_t MetalSleepFn(pm_metal_coro_t *self)
{
  pm_metal_sleep_coro_t *s;
  uint64_t               now;
  uint64_t               deadline;

  s = (pm_metal_sleep_coro_t *)self;
  if (!s->armed) {
    if (self->owner == NULL) {
      return PM_METAL_ERROR;
    }

    now = pm_metal_time_mono_us();
    if (s->absolute) {
      deadline = s->deadline_us;
    } else if (s->us == 0) {
      /* sleep(0)/sleep_us(0): eager DONE — fairness via yield. */
      return PM_METAL_DONE;
    } else {
      deadline = now + s->us;
    }

    if (now >= deadline) {
      return PM_METAL_DONE;
    }

    if (MetalTimerArmAt(deadline, self->owner, NULL, &s->tm) == NULL) {
      return PM_METAL_ERROR;
    }

    s->armed = 1;
    return PM_METAL_WAITING;
  }

  MetalTimerDrop(&s->tm);
  return PM_METAL_DONE;
}

pm_metal_coro_t *pm_metal_sleep_us(uint64_t us)
{
  pm_metal_sleep_coro_t *s;

  s = (pm_metal_sleep_coro_t *)pm_metal_coro(MetalSleepFn, sizeof(*s));
  if (s == NULL) {
    return NULL;
  }

  s->us           = us;
  s->absolute     = 0;
  s->coro.release = MetalSleepRelease;
  return &s->coro;
}

pm_metal_coro_t *pm_metal_sleep_until_us(uint64_t deadline_us)
{
  pm_metal_sleep_coro_t *s;

  s = (pm_metal_sleep_coro_t *)pm_metal_coro(MetalSleepFn, sizeof(*s));
  if (s == NULL) {
    return NULL;
  }

  s->deadline_us  = deadline_us;
  s->absolute     = 1;
  s->coro.release = MetalSleepRelease;
  return &s->coro;
}

pm_metal_coro_t *pm_metal_sleep(uint32_t ms)
{
  return pm_metal_sleep_us((uint64_t)ms * 1000u);
}

static pm_metal_status_t MetalYieldFn(pm_metal_coro_t *self)
{
  pm_metal_yield_coro_t *y;
  pm_metal_task_t       *task;

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
  if (pm_metal_task_spawn(task, task->cpu) != 0) {
    return PM_METAL_ERROR;
  }

  y->armed = 1;
  return PM_METAL_WAITING;
}

pm_metal_coro_t *pm_metal_yield(void)
{
  pm_metal_yield_coro_t *y;

  y = (pm_metal_yield_coro_t *)pm_metal_coro(MetalYieldFn, sizeof(*y));
  if (y == NULL) {
    return NULL;
  }

  return &y->coro;
}
