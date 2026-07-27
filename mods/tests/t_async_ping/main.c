/*
 * Guest proof — ICMP ping (requires replying host; not run in default EFI boot).
 */
#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/net/ping/ping.h"
#include "pymergetic/metal/shell/shell/shell.h"

#define PING_HOST    "10.0.2.2"
#define PING_TIMEOUT 5000u

typedef struct {
  uint32_t step;
  uint32_t aw;
} guest_state_t;

pm_metal_status_t pm_metal_guest_step(pm_metal_async_handle_t self_h)
{
  guest_state_t *s;
  uint32_t       rtt;

  s = pm_metal_async_coro_frame(self_h, (uint32_t)sizeof(*s));

  if (s == NULL) {

    return PM_METAL_ERROR;
  }

  switch (s->step) {
  case 0:
    s->aw = pm_metal_net_ping(PING_HOST, PING_TIMEOUT);
    if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      return PM_METAL_ERROR;
    }
    s->step = 1;
    return pm_metal_async_await(self_h, s->aw);

  case 1:
    rtt = pm_metal_net_ping_rtt_ms(s->aw);
    if (rtt == 0u) {
      return PM_METAL_ERROR;
    }
    pm_metal_shell_log("metal-async: ping ok");
    return PM_METAL_DONE;

  default:
    return PM_METAL_ERROR;
  }
}

int main(void)
{
  return 0;
}

#include "pymergetic/metal/boot/authors.h"
#include "pymergetic/metal/guest/mod/mod.h"

int32_t pm_metal_mod_on_load(void)
{
  pm_metal_mod_set_about_kernel();

  if (pm_metal_mod_register_func("run", "pm_metal_guest_step") != 0) {
    return -1;
  }

  if (pm_metal_mod_register_cmd("async_ping", "run", "async_ping mod command") != 0) {
    return -1;
  }

  return 0;
}

int32_t pm_metal_mod_on_unload(void)
{
  return 0;
}
