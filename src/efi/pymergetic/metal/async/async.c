/** @file
  Guest async ABI — handle tables + trampoline into pm_metal_guest_step. (impl: efi)
**/
#include <pymergetic/metal/async.h>
#include <coro/coro.h>
#include <task/task.h>
#include <run/run.h>
#include <time/time.h>
#include <mem/mem.h>

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
#define PM_METAL_ASYNC_ROOT_STATE  64u
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

  g = (pm_metal_guest_coro_t *)self;
  if (!mActive || mExecEnv == NULL || mStepFn == NULL) {
    return PM_METAL_ERROR;
  }

  argv[0] = g->self_h;
  if (!wasm_runtime_call_wasm (mExecEnv, mStepFn, 1, argv)) {
    CONST CHAR8  *exc;
    UINT32        code;

    exc  = wasm_runtime_get_exception (mInst);
    code = wasm_runtime_get_wasi_exit_code (mInst);
    /*
     * Only wasi proc_exit is a clean finish. Traps also leave exit_code=0
     * — do not treat those as DONE (that hid doom Create failures).
     */
    if (exc != NULL && AsciiStrStr (exc, "wasi proc exit") != NULL) {
      return (code == 0) ? PM_METAL_DONE : PM_METAL_ERROR;
    }

    Print (L"metal-async: guest_step failed: %a (wasi_exit=%u)\r\n",
           exc != NULL ? exc : "?",
           code);
    return PM_METAL_ERROR;
  }

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
pm_metal_async_sleep (
  uint32_t  ms
  )
{
  pm_metal_coro_t         *c;
  pm_metal_async_handle_t  h;

  c = pm_metal_sleep (ms);
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  h = MetalAsyncAlloc (PM_METAL_ASYNC_SLOT_HOST_CORO, c);
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_coro_close (c);
  }

  return h;
}

pm_metal_async_handle_t
pm_metal_async_yield (
  VOID
  )
{
  pm_metal_coro_t         *c;
  pm_metal_async_handle_t  h;

  c = pm_metal_yield ();
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  h = MetalAsyncAlloc (PM_METAL_ASYNC_SLOT_HOST_CORO, c);
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_coro_close (c);
  }

  return h;
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

  /* Pin to CPU0 — shell/wasm pump only drains that inbox. */
  t = pm_metal_task_new (c);
  if (t == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (pm_metal_task_spawn (t, 0) != 0) {
    c->owner = NULL;
    pm_metal_mem_free (t);
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

STATIC NativeSymbol g_pm_metal_async_native_symbols[] = {
  { "pm_metal_async_coro_create", (VOID *)pm_metal_async_coro_create_native, "(i)i", NULL },
  { "pm_metal_async_coro_state", (VOID *)pm_metal_async_coro_state_native, "(i)i", NULL },
  { "pm_metal_async_coro_close", (VOID *)pm_metal_async_coro_close_native, "(i)", NULL },
  { "pm_metal_async_sleep", (VOID *)pm_metal_async_sleep_native, "(i)i", NULL },
  { "pm_metal_async_yield", (VOID *)pm_metal_async_yield_native, "()i", NULL },
  { "pm_metal_async_await", (VOID *)pm_metal_async_await_native, "(ii)i", NULL },
  { "pm_metal_async_create_task", (VOID *)pm_metal_async_create_task_native, "(i)i", NULL },
  { "pm_metal_async_await_task", (VOID *)pm_metal_async_await_task_native, "(ii)i", NULL },
  { "pm_metal_async_task_cancel", (VOID *)pm_metal_async_task_cancel_native, "(i)", NULL },
  { "pm_metal_async_task_status", (VOID *)pm_metal_async_task_status_native, "(i)i", NULL },
  { "pm_metal_async_mono_ms", (VOID *)pm_metal_async_mono_ms_native, "()I", NULL },
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
  if (!mActive) {
    return;
  }

  pm_metal_run_poll (0);
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
