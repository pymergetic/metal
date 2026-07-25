/** @file
  asyncio-shaped gather + wait_for.
**/
#include "coro_internal.h"

#include <pymergetic/metal/runtime/mem/mem.h>

#include <string.h>

typedef struct {
  pm_metal_coro_t   coro;
  pm_metal_task_t **tasks;
  size_t            n;
  size_t            i;
  uint32_t          step;
} pm_metal_gather_coro_t;

typedef struct {
  pm_metal_coro_t   coro;
  pm_metal_task_t  *child;
  pm_metal_timer_t *tm;
  uint32_t          ms;
  uint32_t          step;
  int               timed_out;
} pm_metal_wait_for_coro_t;

void MetalWaitForOnTimeout(pm_metal_coro_t *wait_for, pm_metal_task_t **child_out)
{
  pm_metal_wait_for_coro_t *w;

  if (child_out != NULL) {
    *child_out = NULL;
  }

  if (wait_for == NULL) {
    return;
  }

  w            = (pm_metal_wait_for_coro_t *)wait_for;
  w->timed_out = 1;
  if (child_out != NULL && w->child != NULL) {
    *child_out = w->child;
    pm_metal_task_ref(w->child);
  }
}

static void MetalGatherRelease(pm_metal_coro_t *self)
{
  pm_metal_gather_coro_t *g;
  size_t                  k;

  g = (pm_metal_gather_coro_t *)self;
  if (g->tasks != NULL) {
    for (k = 0; k < g->n; k++) {
      if (g->tasks[k] != NULL) {
        pm_metal_task_destroy(g->tasks[k]);
        g->tasks[k] = NULL;
      }
    }

    pm_metal_mem_free(g->tasks);
    g->tasks = NULL;
  }

  g->n = 0;
}

static pm_metal_status_t MetalGatherFn(pm_metal_coro_t *self)
{
  pm_metal_gather_coro_t *g;
  pm_metal_status_t       st;

  g = (pm_metal_gather_coro_t *)self;

  if (g->step == 0) {
    size_t k;

    for (k = 0; k < g->n; k++) {
      if (g->tasks[k] == NULL) {
        MetalGatherRelease(self);
        return PM_METAL_ERROR;
      }
    }

    g->step = 1;
    g->i    = 0;
  }

  while (g->i < g->n) {
    st = pm_metal_await_task(self, g->tasks[g->i]);
    if (st == PM_METAL_WAITING) {
      return PM_METAL_WAITING;
    }

    if (st != PM_METAL_DONE) {
      MetalGatherRelease(self);
      return st;
    }

    /* Child task finished; drop it now (result already consumed). */
    pm_metal_task_destroy(g->tasks[g->i]);
    g->tasks[g->i] = NULL;
    g->i++;
  }

  MetalGatherRelease(self);
  return PM_METAL_DONE;
}

static void MetalWaitForRelease(pm_metal_coro_t *self)
{
  pm_metal_wait_for_coro_t *w;

  w = (pm_metal_wait_for_coro_t *)self;
  MetalTimerDrop(&w->tm);
  if (w->child != NULL) {
    pm_metal_task_destroy(w->child);
    w->child = NULL;
  }
}

static pm_metal_status_t MetalWaitForFn(pm_metal_coro_t *self)
{
  pm_metal_wait_for_coro_t *w;
  pm_metal_status_t         st;

  w = (pm_metal_wait_for_coro_t *)self;

  if (w->timed_out) {
    return PM_METAL_ERROR;
  }

  if (w->step == 0) {
    if (w->child == NULL || self->owner == NULL) {
      return PM_METAL_ERROR;
    }

    if (MetalTimerArm(w->ms, self->owner, self, &w->tm) == NULL) {
      return PM_METAL_ERROR;
    }

    w->step = 1;
  }

  st = pm_metal_await_task(self, w->child);
  if (st == PM_METAL_WAITING) {
    return PM_METAL_WAITING;
  }

  MetalTimerDrop(&w->tm);
  if (w->timed_out) {
    return PM_METAL_ERROR;
  }

  /*
    Result may point into the child task. Child is destroyed when this
    wait_for coro is closed (after the parent step has run).
  */
  self->result = pm_metal_task_result(w->child);
  return st;
}

pm_metal_coro_t *pm_metal_gather(pm_metal_coro_t **aws, size_t n)
{
  pm_metal_gather_coro_t *g;
  size_t                  k;

  if (aws == NULL && n != 0) {
    return NULL;
  }

  g = (pm_metal_gather_coro_t *)pm_metal_coro(MetalGatherFn, sizeof(*g));
  if (g == NULL) {
    return NULL;
  }

  g->coro.release = MetalGatherRelease;
  g->n            = n;
  if (n > 0) {
    g->tasks = (pm_metal_task_t **)pm_metal_mem_alloc(
      n * sizeof(pm_metal_task_t *), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (g->tasks == NULL) {
      pm_metal_coro_close(&g->coro);
      return NULL;
    }

    memset(g->tasks, 0, n * sizeof(pm_metal_task_t *));
    for (k = 0; k < n; k++) {
      if (aws[k] == NULL) {
        pm_metal_coro_close(&g->coro);
        return NULL;
      }

      g->tasks[k] = pm_metal_create_task(aws[k]);
      if (g->tasks[k] == NULL) {
        pm_metal_coro_close(&g->coro);
        return NULL;
      }
    }
  }

  return &g->coro;
}

pm_metal_coro_t *pm_metal_wait_for(pm_metal_coro_t *aw, uint32_t ms)
{
  pm_metal_wait_for_coro_t *w;

  if (aw == NULL) {
    return NULL;
  }

  w = (pm_metal_wait_for_coro_t *)pm_metal_coro(MetalWaitForFn, sizeof(*w));
  if (w == NULL) {
    pm_metal_coro_close(aw);
    return NULL;
  }

  w->coro.release = MetalWaitForRelease;
  w->ms           = ms;
  w->child        = pm_metal_create_task(aw);
  if (w->child == NULL) {
    pm_metal_coro_close(&w->coro);
    return NULL;
  }

  return &w->coro;
}
