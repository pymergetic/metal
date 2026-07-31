/** @file
  Async ops — sleep/yield/present/frame/await/task/mono.
**/
#include "async_internal.h"

#include "async_host.h"
#include <pymergetic/metal/guest/process/process.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/time/time.h>

#include <stdint.h>

/*
 * Present coro — runs the chunked LFB/VBE job. Used both inline (nested
 * under the caller's own task, legacy path) and as the root of a
 * dedicated cross-runner task (offload path — see pm_metal_async_present).
 */
typedef struct {
  pm_metal_coro_t        coro;
  pm_metal_gfx_surface_h surface;
  int32_t                begun;
} pm_metal_present_worker_coro_t;

/*
 * Outer coro adopted by the guest/host caller via pm_metal_async_present.
 * When offload is available, it hands the worker to another runner via
 * the existing task machinery (pm_metal_task_new + pm_metal_task_spawn,
 * awaited with pm_metal_await_task) instead of stepping the job itself —
 * so a present spike stalls only this one await, not the whole runner
 * (input, pacing, other tasks pinned to the session CPU keep going).
 * Falls back to the legacy inline path on a single-CPU box or if the
 * task hand-off can't be set up.
 */
typedef struct {
  pm_metal_coro_t        coro;
  pm_metal_gfx_surface_h surface;
  int32_t                begun; /* legacy inline path started */
  pm_metal_task_t       *task;  /* offload path: worker task in flight */
} pm_metal_present_coro_t;

typedef struct {
  pm_metal_coro_t        coro;
  int32_t                phase;
  uint64_t               deadline;
  pm_metal_gfx_surface_h surf;
} pm_metal_frame_coro_t;

/* Diagnostic-only A/B switch (default on) — see `presentoffload` shell cmd. */
static int32_t mPresentOffloadEnabled = 1;

void pm_metal_async_present_offload_set(int on)
{
  mPresentOffloadEnabled = on ? 1 : 0;
}

int pm_metal_async_present_offload_get(void)
{
  return mPresentOffloadEnabled;
}

pm_metal_async_handle_t pm_metal_async_sleep_us(uint64_t us)
{
  pm_metal_coro_t *c;

  mPerfSleepUsSum += us;
  mPerfSleepCount++;

  c = pm_metal_sleep_us(us);
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_async_adopt_host_coro(c);
}

pm_metal_async_handle_t pm_metal_async_sleep_until_us(uint64_t deadline_us)
{
  pm_metal_coro_t *c;
  uint64_t         now;

  now = pm_metal_time_mono_us();
  if (deadline_us > now) {
    mPerfSleepUsSum += deadline_us - now;
  }

  mPerfSleepCount++;
  c = pm_metal_sleep_until_us(deadline_us);
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_async_adopt_host_coro(c);
}

pm_metal_async_handle_t pm_metal_async_sleep(uint32_t ms)
{
  return pm_metal_async_sleep_us((uint64_t)ms * 1000u);
}

pm_metal_async_handle_t pm_metal_async_yield(void)
{
  pm_metal_coro_t *c;

  c = pm_metal_yield();
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_async_adopt_host_coro(c);
}

/* Chunked job step shared by the inline and offload-worker paths. */
static pm_metal_status_t MetalPresentJobStep(pm_metal_gfx_surface_h surface,
                                             int32_t               *begun,
                                             pm_metal_coro_t       *yield_self)
{
  int32_t  more;
  uint64_t t0;

  if (*begun == 0) {
    if (pm_metal_gfx_present_job_begin(surface) != 0) {
      pm_metal_logf("metal-async: present begin surface %u failed (ignored)", (uint32_t)surface);
      return PM_METAL_DONE;
    }

    *begun = 1;
  }

  t0   = pm_metal_time_mono_us();
  more = pm_metal_gfx_present_job_step();
  pm_metal_async_perf_note_present_us(pm_metal_time_mono_us() - t0);

  if (more > 0) {
    /* Chunky leaf done — yield so net/input can pump. */
    return pm_metal_await(yield_self, pm_metal_yield());
  }

  if (more < 0) {
    pm_metal_logf("metal-async: present step surface %u failed (ignored)", (uint32_t)surface);
  }

  return PM_METAL_DONE;
}

/* Offload worker — root coro of a one-shot task spawned on another CPU. */
static pm_metal_status_t MetalPresentWorkerFn(pm_metal_coro_t *self)
{
  pm_metal_present_worker_coro_t *w;

  w = (pm_metal_present_worker_coro_t *)self;
  return MetalPresentJobStep(w->surface, &w->begun, self);
}

/* Legacy inline path: steps the job on the caller's own CPU (fallback for
   single-CPU boxes, or if the offload task hand-off can't be set up). */
static pm_metal_status_t MetalPresentInlineStep(pm_metal_present_coro_t *p, pm_metal_coro_t *self)
{
  pm_metal_status_t st;

  st = MetalPresentJobStep(p->surface, &p->begun, self);
  if (st == PM_METAL_WAITING) {
    return PM_METAL_WAITING;
  }

  pm_metal_async_perf_note_present_cpu(mSessionCpu, 0);
  pm_metal_async_perf_note_present_frame();
  return PM_METAL_DONE;
}

/* Awaits an in-flight offload task; cleans up + reports DONE once it
   settles. Split out since both the just-created and re-entrant (parked)
   cases funnel through the same await/park logic. */
static pm_metal_status_t MetalPresentAwaitOffload(pm_metal_coro_t *self, pm_metal_present_coro_t *p)
{
  pm_metal_status_t st;

  st = pm_metal_await_task(self, p->task);
  if (st == PM_METAL_WAITING) {
    return PM_METAL_WAITING;
  }

  if (st != PM_METAL_DONE) {
    pm_metal_logf("metal-async: offloaded present surface %u failed (ignored)",
                  (uint32_t)p->surface);
  }

  pm_metal_task_destroy(p->task);
  p->task = NULL;
  pm_metal_async_perf_note_present_frame();
  return PM_METAL_DONE;
}

static pm_metal_status_t MetalPresentCoroFn(pm_metal_coro_t *self)
{
  pm_metal_present_coro_t        *p;
  pm_metal_present_worker_coro_t *w;
  pm_metal_task_t                *t;
  unsigned                        n_cpus;
  unsigned                        off_cpu;

  p = (pm_metal_present_coro_t *)self;

  if (p->task != NULL) {
    return MetalPresentAwaitOffload(self, p);
  }

  if (p->begun != 0) {
    /* Already running the legacy inline path (offload wasn't set up). */
    return MetalPresentInlineStep(p, self);
  }

  /* First entry — try to hand this frame's present off to another CPU. */
  n_cpus = pm_metal_mem_n_cpus();
  if (n_cpus > 1u && mPresentOffloadEnabled) {
    w = (pm_metal_present_worker_coro_t *)pm_metal_coro(MetalPresentWorkerFn, sizeof(*w));
    if (w != NULL) {
      w->surface = p->surface;
      w->begun   = 0;

      t = pm_metal_task_new(&w->coro);
      if (t != NULL) {
        off_cpu = (mSessionCpu + 1u) % n_cpus;
        if (pm_metal_task_spawn(t, off_cpu) == 0) {
          p->task = t;
          pm_metal_async_perf_note_present_cpu(off_cpu, 1);
          return MetalPresentAwaitOffload(self, p);
        }

        pm_metal_task_destroy(t);
      } else {
        pm_metal_coro_close(&w->coro);
      }
    }
  }

  /* Offload unavailable — fall back to the original inline behavior. */
  return MetalPresentInlineStep(p, self);
}

pm_metal_async_handle_t pm_metal_async_present(uint32_t surface)
{
  pm_metal_present_coro_t *c;
  pm_metal_gfx_surface_h   s;

  s = (surface == 0) ? PM_METAL_GFX_SURFACE_DEFAULT : surface;
  c = (pm_metal_present_coro_t *)pm_metal_coro(MetalPresentCoroFn, sizeof(*c));
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c->surface = s;
  c->begun   = 0;
  c->task    = NULL;
  return pm_metal_async_adopt_host_coro(&c->coro);
}

static pm_metal_status_t MetalFrameCoroFn(pm_metal_coro_t *self)
{
  pm_metal_frame_coro_t   *f;
  pm_metal_coro_t         *sleep_c;
  pm_metal_present_coro_t *pres;

  f = (pm_metal_frame_coro_t *)self;
  if (f->phase == 0) {
    f->deadline = pm_metal_gfx_frame_next_us();
    f->surf     = pm_metal_gfx_dirty_surface();
    f->phase    = 1;
    sleep_c     = pm_metal_sleep_until_us(f->deadline);
    if (sleep_c == NULL) {
      return PM_METAL_ERROR;
    }

    return pm_metal_await(self, sleep_c);
  }

  if (f->phase == 1) {
    f->phase = 2;
    if (f->surf == 0) {
      return PM_METAL_DONE;
    }

    pres = (pm_metal_present_coro_t *)pm_metal_coro(MetalPresentCoroFn, sizeof(*pres));
    if (pres == NULL) {
      return PM_METAL_ERROR;
    }

    pres->surface = f->surf;
    pres->begun   = 0;
    return pm_metal_await(self, &pres->coro);
  }

  return PM_METAL_DONE;
}

pm_metal_async_handle_t pm_metal_async_frame(void)
{
  pm_metal_frame_coro_t *c;

  c = (pm_metal_frame_coro_t *)pm_metal_coro(MetalFrameCoroFn, sizeof(*c));
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c->phase    = 0;
  c->deadline = 0;
  c->surf     = 0;
  return pm_metal_async_adopt_host_coro(&c->coro);
}

uint32_t pm_metal_async_result_u32(pm_metal_async_handle_t self_h)
{
  pm_metal_coro_t *self;

  self = MetalAsyncGetCoro(self_h);
  if (self == NULL || self->result == NULL) {
    return 0;
  }

  return (uint32_t)(uintptr_t)self->result;
}

void pm_metal_async_set_result_u32(pm_metal_async_handle_t self_h, uint32_t v)
{
  pm_metal_coro_t *self;

  self = MetalAsyncGetCoro(self_h);
  if (self == NULL) {
    return;
  }

  self->result = (void *)(uintptr_t)v;
}

void pm_metal_async_coro_set_release(pm_metal_async_handle_t           h,
                                     pm_metal_async_state_release_fn_t fn)
{
  pm_metal_host_fiber_t *f;

  f = (pm_metal_host_fiber_t *)MetalAsyncGet(h, PM_METAL_ASYNC_SLOT_HOST_FIBER);
  if (f == NULL) {
    return;
  }

  f->state_release = fn;
}

void pm_metal_async_task_set_stop_on_done(pm_metal_async_handle_t task_h, int on)
{
  pm_metal_task_t *t;

  t = (pm_metal_task_t *)MetalAsyncGet(task_h, PM_METAL_ASYNC_SLOT_TASK);
  if (t == NULL) {
    return;
  }

  t->stop_on_done = on ? 1 : 0;
}

pm_metal_status_t pm_metal_async_await(pm_metal_async_handle_t self_h, pm_metal_async_handle_t aw_h)
{
  pm_metal_coro_t *self;
  pm_metal_coro_t *aw;

  self = MetalAsyncGetCoro(self_h);
  aw   = MetalAsyncGetCoro(aw_h);
  if (self == NULL || aw == NULL) {
    return PM_METAL_ERROR;
  }

  /*
   * Keep aw_h in the table until the child release path clears it.
   * Clearing here broke self_h for nested host fibers (boot tests, ping, …).
   */
  return pm_metal_await(self, aw);
}

pm_metal_status_t pm_metal_async_await_coro(pm_metal_coro_t *self, pm_metal_async_handle_t aw_h)
{
  pm_metal_coro_t *aw;

  aw = MetalAsyncGetCoro(aw_h);
  if (self == NULL || aw == NULL) {
    return PM_METAL_ERROR;
  }

  return pm_metal_await(self, aw);
}

pm_metal_async_handle_t pm_metal_async_create_task(pm_metal_async_handle_t coro_h)
{
  pm_metal_coro_t        *c;
  pm_metal_task_t        *t;
  pm_metal_async_handle_t h;

  c = MetalAsyncGetCoro(coro_h);
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  /*
   * No CPU affinity — create_task (and gather/wait_for children) lands on
   * a round-robin runner like everything else. The shared WAMR exec_env
   * is protected by the narrow call-in mutex in async.c, not by pinning
   * work to one CPU (see docs/COOP_MEMORY.md).
   */
  t = pm_metal_create_task(c);
  if (t == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  t->proc_id = pm_metal_process_inherit_id();

  h = MetalAsyncAlloc(PM_METAL_ASYNC_SLOT_TASK, t);
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    /* Task already posted; leave it — session_end will reap. */
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  /*
   * Keep coro slots (HOST_CORO / HOST_FIBER / GUEST_CORO) so await(self_h)
   * and coro_state keep working. session_end skips coro_close when
   * coro.owner != NULL (task-owned).
   */
  (void)coro_h;
  return h;
}

void pm_metal_async_task_set_proc_id(pm_metal_async_handle_t task_h, uint32_t proc_id)
{
  pm_metal_task_t *t;

  t = (pm_metal_task_t *)MetalAsyncGet(task_h, PM_METAL_ASYNC_SLOT_TASK);
  if (t != NULL) {
    t->proc_id = proc_id;
  }
}

uint32_t pm_metal_async_task_proc_id(pm_metal_async_handle_t task_h)
{
  pm_metal_task_t *t;

  t = (pm_metal_task_t *)MetalAsyncGet(task_h, PM_METAL_ASYNC_SLOT_TASK);
  return (t != NULL) ? t->proc_id : 0u;
}

pm_metal_status_t pm_metal_async_await_task(pm_metal_async_handle_t self_h,
                                            pm_metal_async_handle_t task_h)
{
  pm_metal_coro_t *self;
  pm_metal_task_t *task;

  self = MetalAsyncGetCoro(self_h);
  task = (pm_metal_task_t *)MetalAsyncGet(task_h, PM_METAL_ASYNC_SLOT_TASK);
  if (self == NULL || task == NULL) {
    return PM_METAL_ERROR;
  }

  return pm_metal_await_task(self, task);
}

void pm_metal_async_task_cancel(pm_metal_async_handle_t task_h)
{
  pm_metal_task_t *task;

  task = (pm_metal_task_t *)MetalAsyncGet(task_h, PM_METAL_ASYNC_SLOT_TASK);
  if (task != NULL) {
    pm_metal_task_cancel(task);
  }
}

pm_metal_status_t pm_metal_async_task_status(pm_metal_async_handle_t task_h)
{
  pm_metal_task_t *task;

  task = (pm_metal_task_t *)MetalAsyncGet(task_h, PM_METAL_ASYNC_SLOT_TASK);
  if (task == NULL) {
    return PM_METAL_ERROR;
  }

  return pm_metal_task_status(task);
}

uint64_t pm_metal_async_mono_ms(void)
{
  return pm_metal_time_mono_us() / 1000u;
}

uint64_t pm_metal_async_mono_us(void)
{
  return pm_metal_time_mono_us();
}
