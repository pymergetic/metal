/*
 * Guest async proof — start a py job via pymergetic.metal.py and await it.
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

  case 1:
    pm_metal_shell_log("metal-async: py ok");
    return PM_METAL_DONE;

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
