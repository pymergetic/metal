/*
 * Phase 2d proof — guest-to-guest FRESH instances via the dual-ABI
 * pm_metal_mod_fresh_open/resolve/close trio (mod.h). Before this phase
 * a wasm guest calling *another* mod had no FRESH option at all — only
 * pm_metal_mod_fn_process (host-only) did. Opens two independent fresh
 * scopes of fresh_counter, bumps each once, and checks neither sees the
 * other's state (both must read back 1, not 1 then 2).
 */
#include <stdint.h>

#include "pymergetic/metal/guest/mod/mod.h"
#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/shell/shell/shell.h"

typedef struct {
  uint32_t                step;
  pm_metal_mod_fresh_h_t  scope_a;
  pm_metal_mod_fresh_h_t  scope_b;
  pm_metal_async_handle_t coro_a;
  pm_metal_async_handle_t coro_b;
  uint32_t                val_a;
} guest_state_t;

pm_metal_status_t pm_metal_guest_step(pm_metal_async_handle_t self_h)
{
  guest_state_t *s;

  s = pm_metal_async_coro_frame(self_h, (uint32_t)sizeof(*s));
  if (s == NULL) {
    return PM_METAL_ERROR;
  }

  switch (s->step) {
  case 0: {
    pm_metal_mod_fn_h_t fn_a;
    pm_metal_mod_fn_h_t fn_b;

    s->scope_a = pm_metal_mod_fresh_open("fresh_counter");
    s->scope_b = pm_metal_mod_fresh_open("fresh_counter");
    if (s->scope_a == 0 || s->scope_b == 0) {
      return PM_METAL_ERROR;
    }

    fn_a = pm_metal_mod_fresh_resolve(s->scope_a, "bump");
    fn_b = pm_metal_mod_fresh_resolve(s->scope_b, "bump");
    if (fn_a == 0 || fn_b == 0) {
      return PM_METAL_ERROR;
    }

    s->coro_a = pm_metal_mod_fn_coro(fn_a);
    s->coro_b = pm_metal_mod_fn_coro(fn_b);
    if (s->coro_a == PM_METAL_ASYNC_HANDLE_INVALID || s->coro_b == PM_METAL_ASYNC_HANDLE_INVALID) {
      return PM_METAL_ERROR;
    }

    s->step = 1;
    return pm_metal_async_await(self_h, s->coro_a);
  }

  case 1:
    s->val_a = pm_metal_async_result_u32(s->coro_a);
    s->step  = 2;
    return pm_metal_async_await(self_h, s->coro_b);

  case 2: {
    uint32_t val_b = pm_metal_async_result_u32(s->coro_b);

    pm_metal_mod_fresh_close(s->scope_a);
    pm_metal_mod_fresh_close(s->scope_b);

    if (s->val_a != 1u || val_b != 1u) {
      return PM_METAL_ERROR;
    }

    pm_metal_shell_log("metal-async: fresh ok");
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

int32_t pm_metal_mod_on_load(void)
{
  if (pm_metal_mod_register_func("run", "pm_metal_guest_step") != 0) {
    return -1;
  }

  if (pm_metal_mod_register_cmd("fresh_guest", "run", "fresh_guest mod command") != 0) {
    return -1;
  }

  return 0;
}

int32_t pm_metal_mod_on_unload(void)
{
  return 0;
}
