/*
 * Metal ASGI httpd — C server + app registry (C | Py | Wasm).
 * Guest/host dual ABI. See plan: C owns wire; apps are app_h leaves.
 *
 * impl: common — src/pymergetic/metal/net/asgi/asgi_*.c
 */
#ifndef PYMERGETIC_METAL_NET_ASGI_H_
#define PYMERGETIC_METAL_NET_ASGI_H_

#include <stdint.h>

#if !defined(__wasm__)
#include "pymergetic/metal/net/tls/tls.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_NET_ASGI_WASI_MODULE "pymergetic.metal.net.asgi"

typedef uint32_t pm_metal_net_asgi_app_h;
typedef uint32_t pm_metal_net_asgi_srv_h;
#if defined(__wasm__)
typedef uint32_t pm_metal_net_tls_creds_h;
#define PM_METAL_TLS_CREDS_INVALID 0u
#endif

#define PM_METAL_NET_ASGI_APP_INVALID 0u
#define PM_METAL_NET_ASGI_SRV_INVALID 0u

typedef enum {
  PM_METAL_NET_ASGI_RUNNER_C    = 1,
  PM_METAL_NET_ASGI_RUNNER_PY   = 2,
  PM_METAL_NET_ASGI_RUNNER_WASM = 3
} pm_metal_net_asgi_runner_kind_t;

/** C ASGI app: drive scope/receive/send via host helpers (conn cookie). */
typedef int32_t (*pm_metal_net_asgi_c_fn)(void *ctx, uint32_t conn_id);

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_NET_ASGI_IMPORT(name) PM_METAL_WASI_IMPORT(PM_METAL_NET_ASGI_WASI_MODULE, name)

extern pm_metal_net_asgi_srv_h pm_metal_net_asgi_listen(uint32_t             port,
                                                        uint32_t             ifnames_ptr,
                                                        uint32_t             nif,
                                                        pm_metal_net_tls_creds_h creds)
  PM_METAL_NET_ASGI_IMPORT(pm_metal_net_asgi_listen);
extern int32_t pm_metal_net_asgi_mount(pm_metal_net_asgi_srv_h s,
                                       const char             *path,
                                       pm_metal_net_asgi_app_h app)
  PM_METAL_NET_ASGI_IMPORT(pm_metal_net_asgi_mount);
extern int32_t pm_metal_net_asgi_unmount(pm_metal_net_asgi_srv_h s, const char *path)
  PM_METAL_NET_ASGI_IMPORT(pm_metal_net_asgi_unmount);
extern void pm_metal_net_asgi_close(pm_metal_net_asgi_srv_h s)
  PM_METAL_NET_ASGI_IMPORT(pm_metal_net_asgi_close);
extern pm_metal_net_asgi_app_h pm_metal_net_asgi_register_wasm(const char *mod, const char *func)
  PM_METAL_NET_ASGI_IMPORT(pm_metal_net_asgi_register_wasm);
extern void pm_metal_net_asgi_unregister(pm_metal_net_asgi_app_h app)
  PM_METAL_NET_ASGI_IMPORT(pm_metal_net_asgi_unregister);
extern int32_t pm_metal_net_asgi_autoload(void)
  PM_METAL_NET_ASGI_IMPORT(pm_metal_net_asgi_autoload);
/** Reply on the active ASGI connection (set up by the host before wasm coro). */
extern int32_t pm_metal_net_asgi_send_simple(uint32_t    code,
                                            const char *reason,
                                            const char *ctype,
                                            const char *body)
  PM_METAL_NET_ASGI_IMPORT(pm_metal_net_asgi_send_simple);
#else
pm_metal_net_asgi_app_h pm_metal_net_asgi_register_c(pm_metal_net_asgi_c_fn fn, void *ctx);
pm_metal_net_asgi_app_h pm_metal_net_asgi_register_py(uint32_t py_cookie);
pm_metal_net_asgi_app_h pm_metal_net_asgi_register_wasm(const char *mod, const char *func);
void                    pm_metal_net_asgi_unregister(pm_metal_net_asgi_app_h app);

/**
 * Listen on port. ifnames NULL or nif==0 => all interfaces.
 * creds 0 => cleartext; else TLS (when TLS server ready).
 */
pm_metal_net_asgi_srv_h pm_metal_net_asgi_listen(uint32_t             port,
                                                 const char *const   *ifnames,
                                                 uint32_t             nif,
                                                 pm_metal_net_tls_creds_h creds);
int32_t                 pm_metal_net_asgi_mount(pm_metal_net_asgi_srv_h s,
                                                const char             *path,
                                                pm_metal_net_asgi_app_h app);
int32_t                 pm_metal_net_asgi_unmount(pm_metal_net_asgi_srv_h s, const char *path);
void                    pm_metal_net_asgi_close(pm_metal_net_asgi_srv_h s);

/** Load /etc/httpd.json and autostart (or built-in defaults). */
int32_t pm_metal_net_asgi_autoload(void);
int32_t pm_metal_net_asgi_reload(void);

/** Reply on the active ASGI connection (host + wasm dual ABI). */
int32_t pm_metal_net_asgi_send_simple(uint32_t    code,
                                      const char *reason,
                                      const char *ctype,
                                      const char *body);

int32_t pm_metal_net_asgi_native_register(void);

/** Built-in C apps (register once). */
pm_metal_net_asgi_app_h pm_metal_net_asgi_app_health(void);
pm_metal_net_asgi_app_h pm_metal_net_asgi_app_sysinfo(void);
pm_metal_net_asgi_app_h pm_metal_net_asgi_app_static(const char *root);
pm_metal_net_asgi_app_h pm_metal_net_asgi_app_microdot(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_ASGI_H_ */
