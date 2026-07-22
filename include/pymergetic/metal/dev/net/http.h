/*
 * HTTP/HTTPS client — guest/host dual ABI on pm_metal_net_*.
 *
 * Async GET only (stackless). After await on the returned handle:
 *   pm_metal_net_http_status(h)   — HTTP status (200, …), 0 on transport fail
 *   pm_metal_net_http_body_len(h) — response body bytes written to dest
 *
 * impl: common — src/pymergetic/metal/dev/net/http.c
 */
#ifndef PYMERGETIC_METAL_DEV_NET_HTTP_H_
#define PYMERGETIC_METAL_DEV_NET_HTTP_H_

#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_NET_HTTP_WASI_MODULE "pymergetic.metal.net.http"

#if defined(__wasm__)
#define PM_METAL_NET_HTTP_IO_PTR(p) ((uint32_t)(uintptr_t)(p))
#endif

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_NET_HTTP_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_NET_HTTP_WASI_MODULE, name)

/** Fetch URL into guest buffer at dest (linear offset). */
extern pm_metal_async_handle_t pm_metal_net_http_get(const char *url,
						     uint32_t dest,
						     uint32_t dest_cap)
	PM_METAL_NET_HTTP_IMPORT(pm_metal_net_http_get);
/** After await: HTTP status (200, …), 0 on transport fail. */
extern uint32_t pm_metal_net_http_status(pm_metal_async_handle_t h)
	PM_METAL_NET_HTTP_IMPORT(pm_metal_net_http_status);
/** After await: response body bytes written to dest. */
extern uint32_t pm_metal_net_http_body_len(pm_metal_async_handle_t h)
	PM_METAL_NET_HTTP_IMPORT(pm_metal_net_http_body_len);
#else
pm_metal_async_handle_t pm_metal_net_http_get(const char *url, void *dest,
					      uint32_t dest_cap);
/** After await: HTTP status (200, …), 0 on transport fail. */
uint32_t pm_metal_net_http_status(pm_metal_async_handle_t h);
/** After await: response body bytes written to dest. */
uint32_t pm_metal_net_http_body_len(pm_metal_async_handle_t h);

int pm_metal_net_http_native_register(void);
void pm_metal_net_http_bind_inst(void *module_inst);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_NET_HTTP_H_ */
