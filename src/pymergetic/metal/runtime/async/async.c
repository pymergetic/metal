/** @file
  Guest async ABI — handle tables + trampoline into pm_metal_guest_step. (impl: efi|bios)
**/
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/dev/audio/audio_ops.h>
#include <pymergetic/metal/boot/port.h>
#include <runtime/coro/coro.h>
#include <runtime/task/task.h>
#include <runtime/run/run.h>
#include <runtime/time/time.h>
#include <runtime/mem/mem.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiLib.h>

#include "wasm_export.h"

#ifndef PM_METAL_ASYNC_MAX_HANDLES
#define PM_METAL_ASYNC_MAX_HANDLES  64u
#endif

#ifndef PM_METAL_ASYNC_ROOT_STATE
#define PM_METAL_ASYNC_ROOT_STATE  1024u
#endif

typedef enum {
  PM_METAL_ASYNC_SLOT_FREE = 0,
  PM_METAL_ASYNC_SLOT_GUEST_CORO,
  PM_METAL_ASYNC_SLOT_HOST_CORO,
  PM_METAL_ASYNC_SLOT_TASK
} pm_metal_async_slot_kind_t;

typedef struct {
  pm_metal_coro_t  coro;
  uint32_t         self_h;
  uint32_t         guest_state; /* linear offset; 0 if none */
  uint32_t         state_bytes;
} pm_metal_guest_coro_t;

typedef struct {
  pm_metal_async_slot_kind_t  kind;
  VOID                       *ptr;
} pm_metal_async_slot_t;

STATIC pm_metal_async_slot_t  mSlots[PM_METAL_ASYNC_MAX_HANDLES + 1];
STATIC wasm_module_inst_t     mInst;
STATIC wasm_exec_env_t        mExecEnv;
STATIC wasm_function_inst_t   mStepFn;
STATIC pm_metal_async_handle_t mRootCoroH;
STATIC pm_metal_async_handle_t mRootTaskH;
STATIC INT32                  mActive;

/* Session perf window (serial once/sec while guest steps). */
STATIC UINT64  mPerfWinStartUs;
STATIC UINT64  mPerfLastStepEndUs;
STATIC UINT32  mPerfSteps;
STATIC UINT64  mPerfStepUsSum;
STATIC UINT64  mPerfGapUsSum;
STATIC UINT64  mPerfBlitUsSum;
STATIC UINT64  mPerfPresentUsSum;
STATIC UINT32  mPerfPresentFrames;
STATIC UINT64  mPerfSleepUsSum;
STATIC UINT32  mPerfSleepCount;
STATIC UINT64  mPerfPumpUsSum;
STATIC UINT32  mPerfPumps;

STATIC
VOID
MetalAsyncPerfReset (
  UINT64  now_us
  )
{
  mPerfWinStartUs     = now_us;
  mPerfLastStepEndUs  = 0;
  mPerfSteps          = 0;
  mPerfStepUsSum      = 0;
  mPerfGapUsSum       = 0;
  mPerfBlitUsSum      = 0;
  mPerfPresentUsSum   = 0;
  mPerfPresentFrames  = 0;
  mPerfSleepUsSum     = 0;
  mPerfSleepCount     = 0;
  mPerfPumpUsSum      = 0;
  mPerfPumps          = 0;
}

STATIC
VOID
MetalAsyncPerfMaybeReport (
  UINT64  now_us
  )
{
  UINT64  elapsed;
  UINT32  step_hz;
  UINT32  frame_hz;
  UINT32  rt_us;
  UINT32  step_us;
  UINT32  blit_us;
  UINT32  present_us;
  UINT32  gap_us;
  UINT32  sleep_us;
  UINT32  pump_us;

  if (mPerfWinStartUs == 0) {
    MetalAsyncPerfReset (now_us);
    return;
  }

  elapsed = now_us - mPerfWinStartUs;
  if (elapsed < 1000000u || mPerfSteps == 0) {
    return;
  }

  step_hz  = (UINT32)(((UINT64)mPerfSteps * 1000000u) / elapsed);
  frame_hz = (mPerfPresentFrames > 0)
               ? (UINT32)(((UINT64)mPerfPresentFrames * 1000000u) / elapsed)
               : 0;
  step_us = (UINT32)(mPerfStepUsSum / mPerfSteps);
  blit_us = (UINT32)(mPerfBlitUsSum / mPerfSteps);
  gap_us  = (UINT32)(mPerfGapUsSum / mPerfSteps);
  rt_us   = step_us + gap_us;
  present_us = (mPerfPresentFrames > 0)
                 ? (UINT32)(mPerfPresentUsSum / mPerfPresentFrames)
                 : 0;
  sleep_us = (mPerfSleepCount > 0)
               ? (UINT32)(mPerfSleepUsSum / mPerfSleepCount)
               : 0;
  pump_us  = (mPerfPumps > 0)
               ? (UINT32)(mPerfPumpUsSum / mPerfPumps)
               : 0;

  {
    CHAR8  line[220];

    AsciiSPrint (
      line,
      sizeof (line),
      "metal-perf: frame_hz=%u step_hz=%u step=%uus blit=%uus present=%uus sleep=%uus gap=%uus rt=%uus pumps=%u pump=%uus",
      frame_hz,
      step_hz,
      step_us,
      blit_us,
      present_us,
      sleep_us,
      gap_us,
      rt_us,
      mPerfPumps,
      pump_us
      );

    /* UART under guest focus / owned mode — ConOut paints GOP / dies post-EBS. */
    if (pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_GUEST || pm_metal_port_owned ()) {
      pm_metal_shell_serial_log (line);
    } else {
      Print (L"%a\r\n", line);
    }
  }

  MetalAsyncPerfReset (now_us);
}

void
pm_metal_async_perf_note_blit_us (
  uint64_t  us
  )
{
  mPerfBlitUsSum += us;
}

void
pm_metal_async_perf_note_present_us (
  uint64_t  us
  )
{
  mPerfPresentUsSum += us;
}

void
pm_metal_async_perf_note_present_frame (
  VOID
  )
{
  mPerfPresentFrames++;
}

STATIC
pm_metal_async_handle_t
MetalAsyncAlloc (
  pm_metal_async_slot_kind_t  kind,
  VOID                       *ptr
  )
{
  UINT32  i;

  if (ptr == NULL || kind == PM_METAL_ASYNC_SLOT_FREE) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  for (i = 1; i <= PM_METAL_ASYNC_MAX_HANDLES; i++) {
    if (mSlots[i].kind == PM_METAL_ASYNC_SLOT_FREE) {
      mSlots[i].kind = kind;
      mSlots[i].ptr  = ptr;
      return (pm_metal_async_handle_t)i;
    }
  }

  return PM_METAL_ASYNC_HANDLE_INVALID;
}

STATIC
VOID
MetalAsyncClear (
  pm_metal_async_handle_t  h
  )
{
  if (h == PM_METAL_ASYNC_HANDLE_INVALID || h > PM_METAL_ASYNC_MAX_HANDLES) {
    return;
  }

  mSlots[h].kind = PM_METAL_ASYNC_SLOT_FREE;
  mSlots[h].ptr  = NULL;
}

STATIC
VOID *
MetalAsyncGet (
  pm_metal_async_handle_t     h,
  pm_metal_async_slot_kind_t  kind
  )
{
  if (h == PM_METAL_ASYNC_HANDLE_INVALID || h > PM_METAL_ASYNC_MAX_HANDLES) {
    return NULL;
  }

  if (mSlots[h].kind != kind) {
    return NULL;
  }

  return mSlots[h].ptr;
}

STATIC
pm_metal_coro_t *
MetalAsyncGetCoro (
  pm_metal_async_handle_t  h
  )
{
  VOID  *p;

  p = MetalAsyncGet (h, PM_METAL_ASYNC_SLOT_GUEST_CORO);
  if (p != NULL) {
    return &((pm_metal_guest_coro_t *)p)->coro;
  }

  return (pm_metal_coro_t *)MetalAsyncGet (h, PM_METAL_ASYNC_SLOT_HOST_CORO);
}

STATIC
pm_metal_status_t
MetalGuestCoroFn (
  pm_metal_coro_t  *self
  )
{
  pm_metal_guest_coro_t  *g;
  UINT32                  argv[1];
  UINT64                  t0;
  UINT64                  t1;

  g = (pm_metal_guest_coro_t *)self;
  if (!mActive || mExecEnv == NULL || mStepFn == NULL) {
    return PM_METAL_ERROR;
  }

  t0 = pm_metal_time_mono_us ();
  if (mPerfLastStepEndUs != 0) {
    mPerfGapUsSum += t0 - mPerfLastStepEndUs;
  }

  argv[0] = g->self_h;
  if (!wasm_runtime_call_wasm (mExecEnv, mStepFn, 1, argv)) {
    CONST CHAR8  *exc;
    UINT32        code;

    exc  = wasm_runtime_get_exception (mInst);
    code = wasm_runtime_get_wasi_exit_code (mInst);
    /*
     * Only wasi proc_exit is a clean finish. Traps also leave exit_code=0
     * — do not treat those as DONE (that hid guest Create failures).
     */
    if (exc != NULL && AsciiStrStr (exc, "wasi proc exit") != NULL) {
      return (code == 0) ? PM_METAL_DONE : PM_METAL_ERROR;
    }

    {
      CHAR8  msg[160];

      AsciiSPrint (
        msg,
        sizeof (msg),
        "metal-async: guest_step failed: %a (wasi_exit=%u)",
        exc != NULL ? exc : "?",
        code
        );
      if (pm_metal_port_owned ()) {
        pm_metal_shell_serial_log (msg);
      } else {
        Print (L"%a\r\n", msg);
      }
    }
    return PM_METAL_ERROR;
  }

  t1 = pm_metal_time_mono_us ();
  mPerfStepUsSum     += t1 - t0;
  mPerfLastStepEndUs  = t1;
  mPerfSteps++;
  MetalAsyncPerfMaybeReport (t1);

  return (pm_metal_status_t)argv[0];
}

STATIC
VOID
MetalGuestCoroRelease (
  pm_metal_coro_t  *self
  )
{
  pm_metal_guest_coro_t  *g;

  g = (pm_metal_guest_coro_t *)self;
  if (mInst != NULL && g->guest_state != 0) {
    wasm_runtime_module_free (mInst, (UINT64)g->guest_state);
    g->guest_state = 0;
  }

  if (g->self_h != PM_METAL_ASYNC_HANDLE_INVALID) {
    MetalAsyncClear (g->self_h);
    g->self_h = PM_METAL_ASYNC_HANDLE_INVALID;
  }
}

pm_metal_async_handle_t
pm_metal_async_coro_create (
  uint32_t  state_bytes
  )
{
  pm_metal_guest_coro_t  *g;
  pm_metal_async_handle_t h;
  VOID                   *native;
  UINT64                  off;

  if (!mActive || mInst == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  g = (pm_metal_guest_coro_t *)pm_metal_coro (
                                 MetalGuestCoroFn,
                                 sizeof (*g)
                                 );
  if (g == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  g->coro.release = MetalGuestCoroRelease;
  g->self_h       = PM_METAL_ASYNC_HANDLE_INVALID;
  g->guest_state  = 0;
  g->state_bytes  = state_bytes;

  if (state_bytes > 0) {
    native = NULL;
    off    = wasm_runtime_module_malloc (mInst, state_bytes, &native);
    if (off == 0 || native == NULL) {
      pm_metal_coro_close (&g->coro);
      return PM_METAL_ASYNC_HANDLE_INVALID;
    }

    ZeroMem (native, state_bytes);
    g->guest_state = (UINT32)off;
  }

  h = MetalAsyncAlloc (PM_METAL_ASYNC_SLOT_GUEST_CORO, g);
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_coro_close (&g->coro);
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  g->self_h = h;
  return h;
}

uint32_t
pm_metal_async_coro_state (
  pm_metal_async_handle_t  h
  )
{
  pm_metal_guest_coro_t  *g;

  g = (pm_metal_guest_coro_t *)MetalAsyncGet (h, PM_METAL_ASYNC_SLOT_GUEST_CORO);
  if (g == NULL) {
    return 0;
  }

  return g->guest_state;
}

void
pm_metal_async_coro_close (
  pm_metal_async_handle_t  h
  )
{
  pm_metal_guest_coro_t  *g;
  pm_metal_coro_t        *c;

  g = (pm_metal_guest_coro_t *)MetalAsyncGet (h, PM_METAL_ASYNC_SLOT_GUEST_CORO);
  if (g != NULL) {
    /* Owned by a task — destroy via task path. */
    if (g->coro.owner != NULL) {
      return;
    }

    pm_metal_coro_close (&g->coro);
    return;
  }

  c = (pm_metal_coro_t *)MetalAsyncGet (h, PM_METAL_ASYNC_SLOT_HOST_CORO);
  if (c != NULL) {
    MetalAsyncClear (h);
    pm_metal_coro_close (c);
  }
}

pm_metal_async_handle_t
pm_metal_async_adopt_host_coro (
  pm_metal_coro_t  *c
  )
{
  pm_metal_async_handle_t  h;

  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  h = MetalAsyncAlloc (PM_METAL_ASYNC_SLOT_HOST_CORO, c);
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_coro_close (c);
  }

  return h;
}

pm_metal_coro_t *
pm_metal_async_host_coro (
  pm_metal_async_handle_t  h
  )
{
  return MetalAsyncGetCoro (h);
}

pm_metal_async_handle_t
pm_metal_async_sleep_us (
  uint64_t  us
  )
{
  pm_metal_coro_t  *c;

  mPerfSleepUsSum += us;
  mPerfSleepCount++;

  c = pm_metal_sleep_us (us);
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_async_adopt_host_coro (c);
}

pm_metal_async_handle_t
pm_metal_async_sleep_until_us (
  uint64_t  deadline_us
  )
{
  pm_metal_coro_t  *c;
  UINT64            now;

  now = pm_metal_time_mono_us ();
  if (deadline_us > now) {
    mPerfSleepUsSum += deadline_us - now;
  }

  mPerfSleepCount++;
  c = pm_metal_sleep_until_us (deadline_us);
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_async_adopt_host_coro (c);
}

pm_metal_async_handle_t
pm_metal_async_sleep (
  uint32_t  ms
  )
{
  return pm_metal_async_sleep_us ((uint64_t)ms * 1000u);
}

pm_metal_async_handle_t
pm_metal_async_yield (
  VOID
  )
{
  pm_metal_coro_t  *c;

  c = pm_metal_yield ();
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_async_adopt_host_coro (c);
}

typedef struct {
  pm_metal_coro_t         coro;
  pm_metal_gfx_surface_h  surface;
  INT32                   begun;
} pm_metal_present_coro_t;

STATIC
pm_metal_status_t
MetalPresentCoroFn (
  pm_metal_coro_t  *self
  )
{
  pm_metal_present_coro_t  *p;
  INT32                     more;
  UINT64                    t0;

  p = (pm_metal_present_coro_t *)self;
  if (p->begun == 0) {
    if (pm_metal_gfx_present_job_begin (p->surface) != 0) {
      pm_metal_logf (
        "metal-async: present begin surface %u failed (ignored)",
        (UINT32)p->surface
        );
      pm_metal_async_perf_note_present_frame ();
      return PM_METAL_DONE;
    }

    p->begun = 1;
  }

  t0   = pm_metal_time_mono_us ();
  more = pm_metal_gfx_present_job_step ();
  pm_metal_async_perf_note_present_us (pm_metal_time_mono_us () - t0);

  if (more > 0) {
    /* Chunky leaf done — yield so net/input can pump. */
    return pm_metal_await (self, pm_metal_yield ());
  }

  if (more < 0) {
    pm_metal_logf (
      "metal-async: present step surface %u failed (ignored)",
      (UINT32)p->surface
      );
  }

  pm_metal_async_perf_note_present_frame ();
  return PM_METAL_DONE;
}

pm_metal_async_handle_t
pm_metal_async_present (
  uint32_t  surface
  )
{
  pm_metal_present_coro_t  *c;
  pm_metal_gfx_surface_h    s;

  s = (surface == 0) ? PM_METAL_GFX_SURFACE_DEFAULT : surface;
  c = (pm_metal_present_coro_t *)pm_metal_coro (
                                   MetalPresentCoroFn,
                                   sizeof (*c)
                                   );
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c->surface = s;
  c->begun   = 0;
  return pm_metal_async_adopt_host_coro (&c->coro);
}

typedef struct {
  pm_metal_coro_t         coro;
  INT32                   phase;
  UINT64                  deadline;
  pm_metal_gfx_surface_h  surf;
} pm_metal_frame_coro_t;

STATIC
pm_metal_status_t
MetalFrameCoroFn (
  pm_metal_coro_t  *self
  )
{
  pm_metal_frame_coro_t  *f;
  pm_metal_coro_t        *sleep_c;
  pm_metal_present_coro_t *pres;

  f = (pm_metal_frame_coro_t *)self;
  if (f->phase == 0) {
    f->deadline = pm_metal_gfx_frame_next_us ();
    f->surf     = pm_metal_gfx_dirty_surface ();
    f->phase    = 1;
    sleep_c     = pm_metal_sleep_until_us (f->deadline);
    if (sleep_c == NULL) {
      return PM_METAL_ERROR;
    }

    return pm_metal_await (self, sleep_c);
  }

  if (f->phase == 1) {
    f->phase = 2;
    if (f->surf == 0) {
      return PM_METAL_DONE;
    }

    pres = (pm_metal_present_coro_t *)pm_metal_coro (
                                       MetalPresentCoroFn,
                                       sizeof (*pres)
                                       );
    if (pres == NULL) {
      return PM_METAL_ERROR;
    }

    pres->surface = f->surf;
    pres->begun   = 0;
    return pm_metal_await (self, &pres->coro);
  }

  return PM_METAL_DONE;
}

pm_metal_async_handle_t
pm_metal_async_frame (
  VOID
  )
{
  pm_metal_frame_coro_t  *c;

  c = (pm_metal_frame_coro_t *)pm_metal_coro (
                                 MetalFrameCoroFn,
                                 sizeof (*c)
                                 );
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c->phase    = 0;
  c->deadline = 0;
  c->surf     = 0;
  return pm_metal_async_adopt_host_coro (&c->coro);
}

uint32_t
pm_metal_async_result_u32 (
  pm_metal_async_handle_t  self_h
  )
{
  pm_metal_coro_t  *self;

  self = MetalAsyncGetCoro (self_h);
  if (self == NULL || self->result == NULL) {
    return 0;
  }

  return (uint32_t)(UINTN)self->result;
}

int32_t
pm_metal_async_await (
  pm_metal_async_handle_t  self_h,
  pm_metal_async_handle_t  aw_h
  )
{
  pm_metal_coro_t  *self;
  pm_metal_coro_t  *aw;

  self = MetalAsyncGetCoro (self_h);
  aw   = MetalAsyncGetCoro (aw_h);
  if (self == NULL || aw == NULL) {
    return (INT32)PM_METAL_ERROR;
  }

  /* Ownership transfers into await; drop our handle table entry. */
  if (mSlots[aw_h].kind == PM_METAL_ASYNC_SLOT_HOST_CORO) {
    MetalAsyncClear (aw_h);
  }

  return (INT32)pm_metal_await (self, aw);
}

int32_t
pm_metal_async_await_coro (
  pm_metal_coro_t         *self,
  pm_metal_async_handle_t  aw_h
  )
{
  pm_metal_coro_t  *aw;

  aw = MetalAsyncGetCoro (aw_h);
  if (self == NULL || aw == NULL) {
    return (INT32)PM_METAL_ERROR;
  }

  if (mSlots[aw_h].kind == PM_METAL_ASYNC_SLOT_HOST_CORO) {
    MetalAsyncClear (aw_h);
  }

  return (INT32)pm_metal_await (self, aw);
}

pm_metal_async_handle_t
pm_metal_async_create_task (
  pm_metal_async_handle_t  coro_h
  )
{
  pm_metal_coro_t         *c;
  pm_metal_task_t         *t;
  pm_metal_async_handle_t  h;

  c = MetalAsyncGetCoro (coro_h);
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  /* Round-robin across equal runners — no CPU0 Extrawurst. */
  t = pm_metal_create_task (c);
  if (t == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  h = MetalAsyncAlloc (PM_METAL_ASYNC_SLOT_TASK, t);
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    /* Task already posted; leave it — session_end will reap. */
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return h;
}

int32_t
pm_metal_async_await_task (
  pm_metal_async_handle_t  self_h,
  pm_metal_async_handle_t  task_h
  )
{
  pm_metal_coro_t  *self;
  pm_metal_task_t  *task;

  self = MetalAsyncGetCoro (self_h);
  task = (pm_metal_task_t *)MetalAsyncGet (task_h, PM_METAL_ASYNC_SLOT_TASK);
  if (self == NULL || task == NULL) {
    return (INT32)PM_METAL_ERROR;
  }

  return (INT32)pm_metal_await_task (self, task);
}

void
pm_metal_async_task_cancel (
  pm_metal_async_handle_t  task_h
  )
{
  pm_metal_task_t  *task;

  task = (pm_metal_task_t *)MetalAsyncGet (task_h, PM_METAL_ASYNC_SLOT_TASK);
  if (task != NULL) {
    pm_metal_task_cancel (task);
  }
}

int32_t
pm_metal_async_task_status (
  pm_metal_async_handle_t  task_h
  )
{
  pm_metal_task_t  *task;

  task = (pm_metal_task_t *)MetalAsyncGet (task_h, PM_METAL_ASYNC_SLOT_TASK);
  if (task == NULL) {
    return (INT32)PM_METAL_ERROR;
  }

  return (INT32)pm_metal_task_status (task);
}

uint64_t
pm_metal_async_mono_ms (
  VOID
  )
{
  return pm_metal_time_mono_us () / 1000u;
}

uint64_t
pm_metal_async_mono_us (
  VOID
  )
{
  return pm_metal_time_mono_us ();
}

/* ---- natives ---- */

STATIC UINT32
pm_metal_async_coro_create_native (
  wasm_exec_env_t  exec_env,
  UINT32           state_bytes
  )
{
  (VOID)exec_env;
  return pm_metal_async_coro_create (state_bytes);
}

STATIC UINT32
pm_metal_async_coro_state_native (
  wasm_exec_env_t  exec_env,
  UINT32           h
  )
{
  (VOID)exec_env;
  return pm_metal_async_coro_state (h);
}

STATIC VOID
pm_metal_async_coro_close_native (
  wasm_exec_env_t  exec_env,
  UINT32           h
  )
{
  (VOID)exec_env;
  pm_metal_async_coro_close (h);
}

STATIC UINT32
pm_metal_async_sleep_native (
  wasm_exec_env_t  exec_env,
  UINT32           ms
  )
{
  (VOID)exec_env;
  return pm_metal_async_sleep (ms);
}

STATIC UINT32
pm_metal_async_sleep_us_native (
  wasm_exec_env_t  exec_env,
  UINT64           us
  )
{
  (VOID)exec_env;
  return pm_metal_async_sleep_us (us);
}

STATIC UINT32
pm_metal_async_sleep_until_us_native (
  wasm_exec_env_t  exec_env,
  UINT64           deadline_us
  )
{
  (VOID)exec_env;
  return pm_metal_async_sleep_until_us (deadline_us);
}

STATIC UINT32
pm_metal_async_present_native (
  wasm_exec_env_t  exec_env,
  UINT32           surface
  )
{
  (VOID)exec_env;
  return pm_metal_async_present (surface);
}

STATIC UINT32
pm_metal_async_frame_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return pm_metal_async_frame ();
}

STATIC UINT32
pm_metal_async_yield_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return pm_metal_async_yield ();
}

STATIC INT32
pm_metal_async_await_native (
  wasm_exec_env_t  exec_env,
  UINT32           self_h,
  UINT32           aw_h
  )
{
  (VOID)exec_env;
  return pm_metal_async_await (self_h, aw_h);
}

STATIC UINT32
pm_metal_async_create_task_native (
  wasm_exec_env_t  exec_env,
  UINT32           coro_h
  )
{
  (VOID)exec_env;
  return pm_metal_async_create_task (coro_h);
}

STATIC INT32
pm_metal_async_await_task_native (
  wasm_exec_env_t  exec_env,
  UINT32           self_h,
  UINT32           task_h
  )
{
  (VOID)exec_env;
  return pm_metal_async_await_task (self_h, task_h);
}

STATIC VOID
pm_metal_async_task_cancel_native (
  wasm_exec_env_t  exec_env,
  UINT32           task_h
  )
{
  (VOID)exec_env;
  pm_metal_async_task_cancel (task_h);
}

STATIC INT32
pm_metal_async_task_status_native (
  wasm_exec_env_t  exec_env,
  UINT32           task_h
  )
{
  (VOID)exec_env;
  return pm_metal_async_task_status (task_h);
}

STATIC UINT64
pm_metal_async_mono_ms_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return pm_metal_async_mono_ms ();
}

STATIC UINT64
pm_metal_async_mono_us_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return pm_metal_async_mono_us ();
}

STATIC UINT32
pm_metal_async_result_u32_native (
  wasm_exec_env_t  exec_env,
  UINT32           self_h
  )
{
  (VOID)exec_env;
  return pm_metal_async_result_u32 (self_h);
}

STATIC NativeSymbol g_pm_metal_async_native_symbols[] = {
  { "pm_metal_async_coro_create", (VOID *)pm_metal_async_coro_create_native, "(i)i", NULL },
  { "pm_metal_async_coro_state", (VOID *)pm_metal_async_coro_state_native, "(i)i", NULL },
  { "pm_metal_async_coro_close", (VOID *)pm_metal_async_coro_close_native, "(i)", NULL },
  { "pm_metal_async_sleep", (VOID *)pm_metal_async_sleep_native, "(i)i", NULL },
  { "pm_metal_async_sleep_us", (VOID *)pm_metal_async_sleep_us_native, "(I)i", NULL },
  { "pm_metal_async_sleep_until_us", (VOID *)pm_metal_async_sleep_until_us_native, "(I)i", NULL },
  { "pm_metal_async_present", (VOID *)pm_metal_async_present_native, "(i)i", NULL },
  { "pm_metal_async_frame", (VOID *)pm_metal_async_frame_native, "()i", NULL },
  { "pm_metal_async_yield", (VOID *)pm_metal_async_yield_native, "()i", NULL },
  { "pm_metal_async_await", (VOID *)pm_metal_async_await_native, "(ii)i", NULL },
  { "pm_metal_async_create_task", (VOID *)pm_metal_async_create_task_native, "(i)i", NULL },
  { "pm_metal_async_await_task", (VOID *)pm_metal_async_await_task_native, "(ii)i", NULL },
  { "pm_metal_async_task_cancel", (VOID *)pm_metal_async_task_cancel_native, "(i)", NULL },
  { "pm_metal_async_task_status", (VOID *)pm_metal_async_task_status_native, "(i)i", NULL },
  { "pm_metal_async_mono_ms", (VOID *)pm_metal_async_mono_ms_native, "()I", NULL },
  { "pm_metal_async_mono_us", (VOID *)pm_metal_async_mono_us_native, "()I", NULL },
  { "pm_metal_async_result_u32", (VOID *)pm_metal_async_result_u32_native, "(i)i", NULL },
};

int
pm_metal_async_native_register (
  VOID
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_ASYNC_WASI_MODULE,
         g_pm_metal_async_native_symbols,
         sizeof (g_pm_metal_async_native_symbols)
           / sizeof (g_pm_metal_async_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}

/* ---- session ---- */

int
pm_metal_async_session_begin (
  VOID   *module_inst,
  VOID   *exec_env,
  VOID   *step_fn
  )
{
  if (mActive || module_inst == NULL || exec_env == NULL || step_fn == NULL) {
    return -1;
  }

  ZeroMem (mSlots, sizeof (mSlots));
  mInst       = (wasm_module_inst_t)module_inst;
  mExecEnv    = (wasm_exec_env_t)exec_env;
  mStepFn     = (wasm_function_inst_t)step_fn;
  mRootCoroH  = PM_METAL_ASYNC_HANDLE_INVALID;
  mRootTaskH  = PM_METAL_ASYNC_HANDLE_INVALID;
  mActive     = 1;
  MetalAsyncPerfReset (pm_metal_time_mono_us ());
  return 0;
}

pm_metal_async_handle_t
pm_metal_async_session_spawn_root (
  VOID
  )
{
  pm_metal_async_handle_t  coro_h;
  pm_metal_async_handle_t  task_h;

  if (!mActive) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  coro_h = pm_metal_async_coro_create (PM_METAL_ASYNC_ROOT_STATE);
  if (coro_h == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  task_h = pm_metal_async_create_task (coro_h);
  if (task_h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_async_coro_close (coro_h);
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  mRootCoroH = coro_h;
  mRootTaskH = task_h;
  return coro_h;
}

void
pm_metal_async_session_pump (
  VOID
  )
{
  UINT64  t0;
  UINT64  t1;

  if (!mActive) {
    return;
  }

  t0 = pm_metal_time_mono_us ();
  pm_metal_net_poll ();
  pm_metal_audio_poll ();
  pm_metal_run_poll_all ();
  t1 = pm_metal_time_mono_us ();
  mPerfPumpUsSum += t1 - t0;
  mPerfPumps++;
}

int
pm_metal_async_session_root_done (
  VOID
  )
{
  pm_metal_status_t  st;

  if (!mActive || mRootTaskH == PM_METAL_ASYNC_HANDLE_INVALID) {
    return 1;
  }

  st = (pm_metal_status_t)pm_metal_async_task_status (mRootTaskH);
  return (st == PM_METAL_DONE
          || st == PM_METAL_ERROR
          || st == PM_METAL_CANCELLED) ? 1 : 0;
}

int32_t
pm_metal_async_session_root_status (
  VOID
  )
{
  if (!mActive || mRootTaskH == PM_METAL_ASYNC_HANDLE_INVALID) {
    return (INT32)PM_METAL_ERROR;
  }

  return pm_metal_async_task_status (mRootTaskH);
}

void
pm_metal_async_session_end (
  VOID
  )
{
  UINT32  i;

  if (!mActive) {
    return;
  }

  if (mRootTaskH != PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_task_t  *t;

    t = (pm_metal_task_t *)MetalAsyncGet (mRootTaskH, PM_METAL_ASYNC_SLOT_TASK);
    MetalAsyncClear (mRootTaskH);
    mRootTaskH = PM_METAL_ASYNC_HANDLE_INVALID;
    mRootCoroH = PM_METAL_ASYNC_HANDLE_INVALID;
    if (t != NULL) {
      pm_metal_task_destroy (t);
    }
  }

  for (i = 1; i <= PM_METAL_ASYNC_MAX_HANDLES; i++) {
    if (mSlots[i].kind == PM_METAL_ASYNC_SLOT_TASK) {
      pm_metal_task_t  *t;

      t = (pm_metal_task_t *)mSlots[i].ptr;
      MetalAsyncClear ((pm_metal_async_handle_t)i);
      if (t != NULL) {
        pm_metal_task_destroy (t);
      }
    } else if (mSlots[i].kind == PM_METAL_ASYNC_SLOT_HOST_CORO) {
      pm_metal_coro_t  *c;

      c = (pm_metal_coro_t *)mSlots[i].ptr;
      MetalAsyncClear ((pm_metal_async_handle_t)i);
      if (c != NULL) {
        pm_metal_coro_close (c);
      }
    } else if (mSlots[i].kind == PM_METAL_ASYNC_SLOT_GUEST_CORO) {
      pm_metal_guest_coro_t  *g;

      g = (pm_metal_guest_coro_t *)mSlots[i].ptr;
      MetalAsyncClear ((pm_metal_async_handle_t)i);
      if (g != NULL && g->coro.owner == NULL) {
        g->self_h = PM_METAL_ASYNC_HANDLE_INVALID;
        pm_metal_coro_close (&g->coro);
      }
    }
  }

  mInst    = NULL;
  mExecEnv = NULL;
  mStepFn  = NULL;
  mActive  = 0;
}

int
pm_metal_async_session_active (
  VOID
  )
{
  return mActive ? 1 : 0;
}
