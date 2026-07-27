/*
 * ASGI app_h registry — C | Py | Wasm runners.
 */
#include "asgi_internal.h"

#include <string.h>

#define ASGI_APP_SLOTS 32u

static asgi_app_slot_t g_apps[ASGI_APP_SLOTS];

asgi_app_slot_t *pm_metal_net_asgi_app_slot(pm_metal_net_asgi_app_h h)
{
  if (h == 0 || h >= ASGI_APP_SLOTS || !g_apps[h].used) {
    return NULL;
  }
  return &g_apps[h];
}

static pm_metal_net_asgi_app_h slot_alloc(void)
{
  uint32_t i;

  for (i = 1; i < ASGI_APP_SLOTS; i++) {
    if (!g_apps[i].used) {
      memset(&g_apps[i], 0, sizeof(g_apps[i]));
      g_apps[i].used = 1;
      return i;
    }
  }
  return PM_METAL_NET_ASGI_APP_INVALID;
}

pm_metal_net_asgi_app_h pm_metal_net_asgi_register_c(pm_metal_net_asgi_c_fn fn, void *ctx)
{
  pm_metal_net_asgi_app_h h;
  asgi_app_slot_t        *s;

  if (fn == NULL) {
    return PM_METAL_NET_ASGI_APP_INVALID;
  }
  h = slot_alloc();
  if (h == PM_METAL_NET_ASGI_APP_INVALID) {
    return h;
  }
  s        = &g_apps[h];
  s->kind  = PM_METAL_NET_ASGI_RUNNER_C;
  s->c_fn  = fn;
  s->c_ctx = ctx;
  return h;
}

pm_metal_net_asgi_app_h pm_metal_net_asgi_register_py(uint32_t py_cookie)
{
  pm_metal_net_asgi_app_h h;
  asgi_app_slot_t        *s;

  h = slot_alloc();
  if (h == PM_METAL_NET_ASGI_APP_INVALID) {
    return h;
  }
  s            = &g_apps[h];
  s->kind      = PM_METAL_NET_ASGI_RUNNER_PY;
  s->py_cookie = py_cookie;
  return h;
}

pm_metal_net_asgi_app_h pm_metal_net_asgi_register_wasm(const char *mod, const char *func)
{
  pm_metal_net_asgi_app_h h;
  asgi_app_slot_t        *s;

  if (mod == NULL || func == NULL) {
    return PM_METAL_NET_ASGI_APP_INVALID;
  }
  h = slot_alloc();
  if (h == PM_METAL_NET_ASGI_APP_INVALID) {
    return h;
  }
  s = &g_apps[h];
  s->kind = PM_METAL_NET_ASGI_RUNNER_WASM;
  strncpy(s->wasm_mod, mod, sizeof(s->wasm_mod) - 1u);
  strncpy(s->wasm_func, func, sizeof(s->wasm_func) - 1u);
  return h;
}

void pm_metal_net_asgi_unregister(pm_metal_net_asgi_app_h app)
{
  if (app == 0 || app >= ASGI_APP_SLOTS) {
    return;
  }
  memset(&g_apps[app], 0, sizeof(g_apps[app]));
}
