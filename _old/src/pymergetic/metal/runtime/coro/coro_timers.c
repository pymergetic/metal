/** @file
  Coop timer list — arm/drop/poll for sleep and wait_for.
**/
#include "coro_internal.h"

#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/time/time.h>
#include <runtime/slot/spin.h>

#include <string.h>

struct pm_metal_timer {
  struct pm_metal_timer  *next;
  uint64_t                deadline_us;
  pm_metal_task_t        *task;
  pm_metal_coro_t        *wait_for; /* non-NULL → timeout path */
  struct pm_metal_timer **owner_slot;
  int                     cancelled;
  int                     linked;
};

static pm_metal_timer_t *mTimers;
static pm_metal_spin_t   mTimerLock;
static int               mTimerLockReady;

static void MetalTimerLockInit(void)
{
  if (!mTimerLockReady) {
    pm_metal_spin_init(&mTimerLock);
    mTimerLockReady = 1;
  }
}

void pm_metal_coro_timers_init(void)
{
  MetalTimerLockInit();
}

static void MetalTimerUnlinkLocked(pm_metal_timer_t *tm)
{
  pm_metal_timer_t **pp;

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
void MetalTimerDrop(pm_metal_timer_t **slot)
{
  pm_metal_timer_t *tm;

  if (slot == NULL) {
    return;
  }

  MetalTimerLockInit();
  pm_metal_spin_lock(&mTimerLock);
  tm = *slot;
  if (tm != NULL) {
    *slot = NULL;
    if (tm->owner_slot == slot) {
      tm->owner_slot = NULL;
    }

    MetalTimerUnlinkLocked(tm);
  }

  pm_metal_spin_unlock(&mTimerLock);

  if (tm != NULL) {
    if (tm->task != NULL) {
      pm_metal_task_unref(tm->task);
      tm->task = NULL;
    }

    pm_metal_mem_free(tm);
  }
}

pm_metal_timer_t *MetalTimerArmAt(uint64_t           deadline_us,
                                  pm_metal_task_t   *task,
                                  pm_metal_coro_t   *wait_for,
                                  pm_metal_timer_t **owner_slot)
{
  pm_metal_timer_t *tm;

  tm = (pm_metal_timer_t *)pm_metal_mem_alloc(sizeof(*tm), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (tm == NULL) {
    return NULL;
  }

  memset(tm, 0, sizeof(*tm));
  tm->deadline_us = deadline_us;
  tm->task        = task;
  tm->wait_for    = wait_for;
  tm->owner_slot  = owner_slot;
  if (owner_slot != NULL) {
    *owner_slot = tm;
  }

  /* Retain owner for the armed timer (METAL-003). */
  if (task != NULL) {
    pm_metal_task_ref(task);
  }

  MetalTimerLockInit();
  pm_metal_spin_lock(&mTimerLock);
  {
    pm_metal_timer_t **pp;

    /*
     * Insertion-sort by deadline (ascending) so poll can peek just the
     * head instead of scanning the whole list every call — arm is the
     * cold path (a handful of timers ever outstanding), poll is the hot
     * one (every idle tick, every CPU).
     */
    pp = &mTimers;
    while (*pp != NULL && (*pp)->deadline_us <= deadline_us) {
      pp = &(*pp)->next;
    }

    tm->next   = *pp;
    *pp        = tm;
    tm->linked = 1;
  }

  pm_metal_spin_unlock(&mTimerLock);
  return tm;
}

pm_metal_timer_t *MetalTimerArm(uint32_t           ms,
                                pm_metal_task_t   *task,
                                pm_metal_coro_t   *wait_for,
                                pm_metal_timer_t **owner_slot)
{
  return MetalTimerArmAt(
    pm_metal_time_mono_us() + (uint64_t)ms * 1000u, task, wait_for, owner_slot);
}

void pm_metal_coro_poll_timers(void)
{
  uint64_t now;

  MetalTimerLockInit();
  now = pm_metal_time_mono_us();

  for (;;) {
    pm_metal_timer_t *tm;
    pm_metal_task_t  *task;
    pm_metal_coro_t  *wait_for;
    int               cancelled;

    /*
     * mTimers is kept sorted ascending by deadline (see MetalTimerArmAt),
     * so the earliest-due timer is always the head — peek it only instead
     * of walking the whole list. If the head isn't due yet, nothing after
     * it is either; early-exit.
     */
    tm = NULL;
    pm_metal_spin_lock(&mTimerLock);
    if (mTimers != NULL && now >= mTimers->deadline_us) {
      tm = mTimers;
      MetalTimerUnlinkLocked(tm);
      if (tm->owner_slot != NULL && *tm->owner_slot == tm) {
        *tm->owner_slot = NULL;
      }

      tm->owner_slot = NULL;
    }

    if (tm == NULL) {
      pm_metal_spin_unlock(&mTimerLock);
      break;
    }

    cancelled    = tm->cancelled;
    task         = tm->task;
    wait_for     = tm->wait_for;
    tm->task     = NULL;
    tm->wait_for = NULL;

    {
      pm_metal_task_t *child;

      child = NULL;
      /*
       * Publish timeout under the timer lock so teardown cannot free
       * wait_for before timed_out is written (METAL-003). Retain child
       * for cancel after unlock.
       */
      if (!cancelled && wait_for != NULL) {
        MetalWaitForOnTimeout(wait_for, &child);
      }

      pm_metal_spin_unlock(&mTimerLock);

      if (!cancelled) {
        if (child != NULL) {
          pm_metal_task_cancel(child);
          pm_metal_task_unref(child);
        }

        if (task != NULL) {
          (void)pm_metal_task_spawn(task, task->cpu);
        }
      }

      if (task != NULL) {
        pm_metal_task_unref(task);
      }

      pm_metal_mem_free(tm);
    }
  }
}
