/*
 * T0 — hello via Metal shell_log (no WASI stdio).
 */
#include "pymergetic/metal/shell/shell/shell.h"
#include "pymergetic/metal/guest/mod/mod.h"
#include "pymergetic/metal/runtime/async/async.h"

pm_metal_status_t hello_run(pm_metal_async_handle_t self_h)
{
  (void)self_h;
  pm_metal_shell_log("t0_hello");
  return PM_METAL_DONE;
}

int32_t pm_metal_mod_on_load(void)
{
  if (pm_metal_mod_register_func("run", "hello_run") != 0) {
    return -1;
  }

  if (pm_metal_mod_register_cmd("hello", "run", "hello proof") != 0) {
    return -1;
  }

  return 0;
}

int32_t pm_metal_mod_on_unload(void)
{
  return 0;
}

int main(void)
{
  return 0;
}
