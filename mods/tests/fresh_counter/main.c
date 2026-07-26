/*
 * Phase 2d proof fixture — MULTI-capability mod with a static counter.
 * Each FRESH instance gets its own wasm linear memory, so g_counter is
 * inherently per-instance: bumping two independently-opened fresh scopes
 * must both start from 0 (see mod_py_bind.c's .fresh() / fresh_guest's
 * guest-ABI trio, both exercised against this mod).
 */
#include <stdint.h>

#include "pymergetic/metal/boot/authors.h"
#include "pymergetic/metal/guest/mod/mod.h"
#include "pymergetic/metal/runtime/async/async.h"

static int32_t g_counter = 0;

pm_metal_status_t pm_metal_fresh_counter_bump(pm_metal_async_handle_t self_h)
{
  g_counter++;
  pm_metal_async_set_result_u32(self_h, (uint32_t)g_counter);
  return PM_METAL_DONE;
}

int main(void)
{
  return 0;
}

int32_t pm_metal_mod_on_load(void)
{
  pm_metal_mod_set_about_kernel();

  if (pm_metal_mod_set_capability(PM_METAL_MOD_CAP_MULTI) != 0) {
    return -1;
  }

  if (pm_metal_mod_register_func("bump", "pm_metal_fresh_counter_bump") != 0) {
    return -1;
  }

  return 0;
}

int32_t pm_metal_mod_on_unload(void)
{
  return 0;
}
