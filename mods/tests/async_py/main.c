/*
 * Guest async proof — start a py job via pymergetic.metal.py and await it,
 * then (phase 2f) resolve + call a specific already-bound Python function
 * (sync only) from inside this same wasm mod. The async guest-call path
 * (pm_metal_py_fn_call_async) is implemented and structurally identical to
 * the pre-existing bound-pointer path py_shell.c's `py` command already
 * uses, but is not exercised here: a separate, pre-existing bug (see
 * docs/MICROPYTHON.md "Known issue — second nested async task hang")
 * hangs the later OOM-isolation boot proof whenever the boot sequence as a
 * whole spawns a *second* coroutine-backed async task anywhere (not
 * specific to Python, not specific to this mod) — reproduces with two
 * plain pm_metal_py_run_script calls and no phase 2f code at all, so it is
 * not a phase 2f regression.
 */
#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/py/py.h"
#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/shell/shell/shell.h"

typedef struct {
  uint32_t step;
  uint32_t aw;
} guest_state_t;

pm_metal_status_t pm_metal_guest_step(pm_metal_async_handle_t self_h)
{
  guest_state_t    *s;
  static const char path[] = "/mods/py/sleep_demo.py";

  s = pm_metal_async_coro_frame(self_h, (uint32_t)sizeof(*s));

  if (s == NULL) {

    return PM_METAL_ERROR;
  }

  switch (s->step) {
  case 0:
    s->aw = pm_metal_py_run_script(path);
    if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      return PM_METAL_ERROR;
    }
    s->step = 1;
    return pm_metal_async_await_task(self_h, s->aw);

  case 1: {
    pm_metal_py_fn_h_t sync_fn;
    int32_t            sum = 0;

    sync_fn = pm_metal_py_fn_resolve("c_py_demo.add");
    if (sync_fn == 0) {
      return PM_METAL_ERROR;
    }
    if (pm_metal_py_fn_call(sync_fn, (uint32_t)(uintptr_t)&sum, 2, 3) != 0 || sum != 5) {
      return PM_METAL_ERROR;
    }

    pm_metal_shell_log("metal-async: py ok");
    return PM_METAL_DONE;
  }

  default:
    return PM_METAL_ERROR;
  }
}

int main(void)
{
  return 0;
}

#include "pymergetic/metal/guest/mod/mod.h"

int32_t pm_metal_mod_on_load(void)
{
  if (pm_metal_mod_register_func("run", "pm_metal_guest_step") != 0) {
    return -1;
  }

  if (pm_metal_mod_register_cmd("async_py", "run", "async_py mod command") != 0) {
    return -1;
  }

  return 0;
}

int32_t pm_metal_mod_on_unload(void)
{
  return 0;
}
