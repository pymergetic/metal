/** @file
  Async wasm session lifecycle + guest-step perf window.
**/
#include "async_internal.h"

#include <stdint.h>
#include <stdio.h>

#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/net/ip/ip_ops.h>
#include <pymergetic/metal/dev/audio/audio_ops.h>
#include <pymergetic/metal/boot/port.h>
#include <runtime/run/run.h>
#include <runtime/time/time.h>
#include <pymergetic/metal/runtime/mem/mem.h>

pm_metal_async_callin_t mCallin;
wasm_module_inst_t      mInst;
wasm_exec_env_t         mExecEnv;
wasm_function_inst_t    mStepFn;
pm_metal_async_handle_t mRootCoroH;
pm_metal_async_handle_t mRootTaskH;
int32_t                 mActive;
unsigned                mSessionCpu;

uint64_t mPerfWinStartUs;
uint64_t mPerfLastStepEndUs;
uint32_t mPerfSteps;
uint64_t mPerfStepUsSum;
uint64_t mPerfGapUsSum;
uint64_t mPerfBlitUsSum;
uint64_t mPerfPresentUsSum;
uint32_t mPerfPresentFrames;
uint64_t mPerfSleepUsSum;
uint32_t mPerfSleepCount;
uint64_t mPerfPumpUsSum;
uint32_t mPerfPumps;
uint64_t mPerfStepUsMax;
uint64_t mPerfGapUsMax;
uint64_t mPerfPresentUsMax;
uint32_t mPerfPresentCpu = 0xFFFFFFFFu;
uint32_t mPerfPresentOffloads;

void pm_metal_async_perf_note_present_cpu(unsigned cpu, int offloaded)
{
  mPerfPresentCpu = (uint32_t)cpu;
  if (offloaded) {
    mPerfPresentOffloads++;
  }
}

void MetalAsyncPerfReset(uint64_t now_us)
{
  mPerfWinStartUs      = now_us;
  mPerfLastStepEndUs   = 0;
  mPerfSteps           = 0;
  mPerfStepUsSum       = 0;
  mPerfGapUsSum        = 0;
  mPerfBlitUsSum       = 0;
  mPerfPresentUsSum    = 0;
  mPerfPresentFrames   = 0;
  mPerfSleepUsSum      = 0;
  mPerfSleepCount      = 0;
  mPerfPumpUsSum       = 0;
  mPerfPumps           = 0;
  mPerfPresentOffloads = 0;
  mPerfStepUsMax       = 0;
  mPerfGapUsMax        = 0;
  mPerfPresentUsMax    = 0;
}

void MetalAsyncPerfMaybeReport(uint64_t now_us)
{
  uint64_t elapsed;
  uint32_t step_hz;
  uint32_t frame_hz;
  uint32_t rt_us;
  uint32_t step_us;
  uint32_t blit_us;
  uint32_t present_us;
  uint32_t gap_us;
  uint32_t sleep_us;
  uint32_t pump_us;
  uint32_t step_max_us;
  uint32_t gap_max_us;
  uint32_t present_max_us;

  if (mPerfWinStartUs == 0) {
    MetalAsyncPerfReset(now_us);
    return;
  }

  elapsed = now_us - mPerfWinStartUs;
  if (elapsed < 1000000u || mPerfSteps == 0) {
    return;
  }

  step_hz = (uint32_t)(((uint64_t)mPerfSteps * 1000000u) / elapsed);
  frame_hz =
    (mPerfPresentFrames > 0) ? (uint32_t)(((uint64_t)mPerfPresentFrames * 1000000u) / elapsed) : 0;
  step_us     = (uint32_t)(mPerfStepUsSum / mPerfSteps);
  blit_us     = (uint32_t)(mPerfBlitUsSum / mPerfSteps);
  gap_us      = (uint32_t)(mPerfGapUsSum / mPerfSteps);
  rt_us       = step_us + gap_us;
  present_us  = (mPerfPresentFrames > 0) ? (uint32_t)(mPerfPresentUsSum / mPerfPresentFrames) : 0;
  sleep_us    = (mPerfSleepCount > 0) ? (uint32_t)(mPerfSleepUsSum / mPerfSleepCount) : 0;
  pump_us     = (mPerfPumps > 0) ? (uint32_t)(mPerfPumpUsSum / mPerfPumps) : 0;
  step_max_us = (uint32_t)mPerfStepUsMax;
  gap_max_us  = (uint32_t)mPerfGapUsMax;
  present_max_us = (uint32_t)mPerfPresentUsMax;

  /* UART under guest focus / owned mode — ConOut paints GOP / dies post-EBS. */
  if (pm_metal_input_focus() == PM_METAL_INPUT_FOCUS_GUEST || pm_metal_port_owned()) {
    char line[300];

    /* Format into UART path only — shell_serial_log takes a finished line. */
    (void)snprintf(line,
                   sizeof(line),
                   "metal-perf: cpu=%u frame_hz=%u step_hz=%u step=%uus blit=%uus present=%uus "
                   "sleep=%uus gap=%uus rt=%uus pumps=%u pump=%uus step_max=%uus gap_max=%uus "
                   "present_max=%uus present_cpu=%u offloads=%u",
                   mSessionCpu,
                   frame_hz,
                   step_hz,
                   step_us,
                   blit_us,
                   present_us,
                   sleep_us,
                   gap_us,
                   rt_us,
                   mPerfPumps,
                   pump_us,
                   step_max_us,
                   gap_max_us,
                   present_max_us,
                   mPerfPresentCpu,
                   mPerfPresentOffloads);
    pm_metal_shell_serial_log(line);
  } else {
    pm_metal_logf("metal-perf: cpu=%u frame_hz=%u step_hz=%u step=%uus blit=%uus present=%uus "
                  "sleep=%uus gap=%uus rt=%uus pumps=%u pump=%uus step_max=%uus gap_max=%uus "
                  "present_max=%uus present_cpu=%u offloads=%u",
                  mSessionCpu,
                  frame_hz,
                  step_hz,
                  step_us,
                  blit_us,
                  present_us,
                  sleep_us,
                  gap_us,
                  rt_us,
                  mPerfPumps,
                  pump_us,
                  step_max_us,
                  gap_max_us,
                  present_max_us,
                  mPerfPresentCpu,
                  mPerfPresentOffloads);
  }

  MetalAsyncPerfReset(now_us);
}

void pm_metal_async_perf_note_blit_us(uint64_t us)
{
  mPerfBlitUsSum += us;
}

void pm_metal_async_perf_note_present_us(uint64_t us)
{
  mPerfPresentUsSum += us;
  if (us > mPerfPresentUsMax) {
    mPerfPresentUsMax = us;
  }
}

void pm_metal_async_perf_note_present_frame(void)
{
  mPerfPresentFrames++;
  pm_metal_gfx_note_frame();
}

void pm_metal_async_perf_reset(void)
{
  MetalAsyncPerfReset(pm_metal_time_mono_us());
}

int pm_metal_async_session_begin(void *module_inst, void *exec_env, void *step_fn)
{
  if (mActive || module_inst == NULL || exec_env == NULL || step_fn == NULL) {
    return -1;
  }

  /*
   * Do not wipe mSlots — host fibers/tasks (boot init, net life, py, …)
   * share the table and keep their self_h across guest call-ins. session_end
   * reaps guest-rooted entries; a full ZeroMem made host self_h point at
   * reused slots and STOP'd the boot task after the first guest proof.
   */
  mCallin.inst     = (wasm_module_inst_t)module_inst;
  mCallin.exec_env = (wasm_exec_env_t)exec_env;
  mCallin.step_fn  = (wasm_function_inst_t)step_fn;
  mInst            = mCallin.inst;
  mExecEnv         = mCallin.exec_env;
  mStepFn          = mCallin.step_fn;
  mRootCoroH       = PM_METAL_ASYNC_HANDLE_INVALID;
  mRootTaskH       = PM_METAL_ASYNC_HANDLE_INVALID;
  /* Diagnostic only (which CPU began the session; `cpu` shell command /
     present-offload target base) — no longer a scheduling constraint. */
  mSessionCpu = pm_metal_mem_cpu();
  mActive     = 1;
  MetalAsyncPerfReset(pm_metal_time_mono_us());
  return 0;
}

pm_metal_async_handle_t pm_metal_async_session_spawn_root_coro(pm_metal_async_handle_t coro_h)
{
  pm_metal_async_handle_t task_h;

  if (!mActive || coro_h == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  task_h = pm_metal_async_create_task(coro_h);
  if (task_h == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  mRootCoroH = coro_h;
  mRootTaskH = task_h;
  return coro_h;
}

pm_metal_async_handle_t pm_metal_async_session_spawn_root(void)
{
  pm_metal_async_handle_t coro_h;

  if (!mActive) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  /* Root is a normal task — no pre-sized frame; guest coro_alloc if needed. */
  coro_h = pm_metal_async_coro_create(NULL, 0u);
  if (coro_h == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (pm_metal_async_session_spawn_root_coro(coro_h) == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_async_coro_close(coro_h);
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return coro_h;
}

pm_metal_async_handle_t pm_metal_async_session_root_task(void)
{
  return mActive ? mRootTaskH : PM_METAL_ASYNC_HANDLE_INVALID;
}

void pm_metal_async_session_pump(void)
{
  uint64_t t0;
  uint64_t t1;

  if (!mActive) {
    return;
  }

  t0 = pm_metal_time_mono_us();
  pm_metal_input_poll();
  pm_metal_net_ip_poll();
  pm_metal_audio_poll();
  /* Guest tasks can land on any runner now (no session pinning) — drain
     every CPU's inbox, not just mSessionCpu. */
  pm_metal_run_poll_all();
  t1 = pm_metal_time_mono_us();
  mPerfPumpUsSum += t1 - t0;
  mPerfPumps++;
}

int pm_metal_async_session_root_done(void)
{
  pm_metal_status_t st;

  if (!mActive || mRootTaskH == PM_METAL_ASYNC_HANDLE_INVALID) {
    return 1;
  }

  st = (pm_metal_status_t)pm_metal_async_task_status(mRootTaskH);
  return (st == PM_METAL_DONE || st == PM_METAL_ERROR || st == PM_METAL_CANCELLED) ? 1 : 0;
}

pm_metal_status_t pm_metal_async_session_root_status(void)
{
  if (!mActive || mRootTaskH == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ERROR;
  }

  return pm_metal_async_task_status(mRootTaskH);
}

/**
 * Guest-rooted async tasks die with the wasm session. Host-fiber tasks
 * (boot init, net life, py jobs, …) share the handle table and must not.
 */
static int32_t MetalAsyncTaskIsGuestRoot(pm_metal_task_t *t)
{
  uint32_t i;
  void    *p;

  if (t == NULL || t->coro == NULL) {
    return 1;
  }

  for (i = 1; i <= PM_METAL_ASYNC_MAX_HANDLES; i++) {
    pm_metal_async_slot_t s;

    s = MetalAsyncSlotPeek(i);
    if (s.kind != PM_METAL_ASYNC_SLOT_GUEST_CORO) {
      continue;
    }

    p = s.ptr;
    if (p != NULL && &((pm_metal_guest_coro_t *)p)->coro == t->coro) {
      return 1;
    }
  }

  return 0;
}

void pm_metal_async_session_end(void)
{
  uint32_t i;

  if (!mActive) {
    return;
  }

  if (mRootTaskH != PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_task_t *t;

    t = (pm_metal_task_t *)MetalAsyncGet(mRootTaskH, PM_METAL_ASYNC_SLOT_TASK);
    MetalAsyncClear(mRootTaskH);
    mRootTaskH = PM_METAL_ASYNC_HANDLE_INVALID;
    mRootCoroH = PM_METAL_ASYNC_HANDLE_INVALID;
    if (t != NULL) {
      pm_metal_task_destroy(t);
    }
  }

  for (i = 1; i <= PM_METAL_ASYNC_MAX_HANDLES; i++) {
    pm_metal_async_slot_t s;

    s = MetalAsyncSlotPeek(i);
    if (s.kind == PM_METAL_ASYNC_SLOT_TASK) {
      pm_metal_task_t *t;

      t = (pm_metal_task_t *)s.ptr;
      if (!MetalAsyncTaskIsGuestRoot(t)) {
        /* Boot / life / other host create_task handles outlive the session. */
        continue;
      }

      MetalAsyncClear((pm_metal_async_handle_t)i);
      if (t != NULL) {
        pm_metal_task_destroy(t);
      }
    } else if (s.kind == PM_METAL_ASYNC_SLOT_HOST_CORO ||
               s.kind == PM_METAL_ASYNC_SLOT_HOST_FIBER) {
      /*
       * Host fibers outlive guest wasm sessions (boot tests, net life, …).
       * Closing them here drops self_h slots and breaks await(self_h).
       */
    } else if (s.kind == PM_METAL_ASYNC_SLOT_GUEST_CORO) {
      pm_metal_guest_coro_t *g;

      g = (pm_metal_guest_coro_t *)s.ptr;
      if (g != NULL && g->coro.owner == NULL && g->coro.waiter == NULL) {
        g->self_h = PM_METAL_ASYNC_HANDLE_INVALID;
        pm_metal_coro_close(&g->coro);
      }
    }
  }

  mCallin.inst     = NULL;
  mCallin.exec_env = NULL;
  mCallin.step_fn  = NULL;
  mInst            = NULL;
  mExecEnv         = NULL;
  mStepFn          = NULL;
  mActive          = 0;
}

int pm_metal_async_session_active(void)
{
  return mActive ? 1 : 0;
}

unsigned pm_metal_async_session_cpu(void)
{
  return mSessionCpu;
}
