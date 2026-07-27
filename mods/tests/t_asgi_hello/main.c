/*
 * Wasm ASGI proof -- register leaf, listen+mount, reply via send_simple.
 * Shell: asgi_hello  (then GET http://<host>:18080/hello)
 */
#include <stdint.h>

#include "pymergetic/metal/boot/authors.h"
#include "pymergetic/metal/dev/net/asgi.h"
#include "pymergetic/metal/guest/mod/mod.h"
#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/shell/shell/shell.h"

#define ASGI_HELLO_MOD  "asgi_hello"
#define ASGI_HELLO_FUNC "asgi_hello_app"
#define ASGI_HELLO_PORT 18080u

static pm_metal_net_asgi_srv_h s_srv;
static pm_metal_net_asgi_app_h s_app;

/** Invoked by the host ASGI wasm runner on each matched request. */
pm_metal_status_t asgi_hello_app(pm_metal_async_handle_t self_h)
{
  (void)self_h;
  if (pm_metal_net_asgi_send_simple(200u, "OK", "text/plain", "asgi-hello\n") != 0) {
    return PM_METAL_ERROR;
  }
  return PM_METAL_DONE;
}

pm_metal_status_t asgi_hello_run(pm_metal_async_handle_t self_h)
{
  (void)self_h;

  if (s_app == PM_METAL_NET_ASGI_APP_INVALID) {
    s_app = pm_metal_net_asgi_register_wasm(ASGI_HELLO_MOD, ASGI_HELLO_FUNC);
    if (s_app == PM_METAL_NET_ASGI_APP_INVALID) {
      pm_metal_shell_log("asgi_hello: register_wasm failed");
      return PM_METAL_ERROR;
    }
  }

  if (s_srv == PM_METAL_NET_ASGI_SRV_INVALID) {
    s_srv = pm_metal_net_asgi_listen(ASGI_HELLO_PORT, 0u, 0u, PM_METAL_TLS_CREDS_INVALID);
    if (s_srv == PM_METAL_NET_ASGI_SRV_INVALID) {
      pm_metal_shell_log("asgi_hello: listen failed");
      return PM_METAL_ERROR;
    }
  }

  if (pm_metal_net_asgi_mount(s_srv, "/hello", s_app) != 0) {
    pm_metal_shell_log("asgi_hello: mount failed");
    return PM_METAL_ERROR;
  }

  pm_metal_shell_log("asgi_hello: listening :18080/hello");
  return PM_METAL_DONE;
}

int32_t pm_metal_mod_on_load(void)
{
  pm_metal_mod_set_about_kernel();

  if (pm_metal_mod_register_func(ASGI_HELLO_FUNC, ASGI_HELLO_FUNC) != 0) {
    return -1;
  }
  if (pm_metal_mod_register_func("run", "asgi_hello_run") != 0) {
    return -1;
  }
  if (pm_metal_mod_register_cmd("asgi_hello", "run", "wasm ASGI hello listen+mount") != 0) {
    return -1;
  }

  return 0;
}

int32_t pm_metal_mod_on_unload(void)
{
  if (s_srv != PM_METAL_NET_ASGI_SRV_INVALID) {
    pm_metal_net_asgi_close(s_srv);
    s_srv = PM_METAL_NET_ASGI_SRV_INVALID;
  }
  if (s_app != PM_METAL_NET_ASGI_APP_INVALID) {
    pm_metal_net_asgi_unregister(s_app);
    s_app = PM_METAL_NET_ASGI_APP_INVALID;
  }
  return 0;
}

int main(void)
{
  return 0;
}
