/*
 * HTTP(S) client — Metal net API. Same for guests (wasi import) and host.
 */
#ifndef PYMERGETIC_METAL_NET_HTTP_H_
#define PYMERGETIC_METAL_NET_HTTP_H_

#include <stddef.h>

#include "pymergetic/metal/wasi.h" /* IWYU pragma: keep */

#define PM_METAL_NET_HTTP_WASI_MODULE "pymergetic.metal.net.http"

#if defined(__wasm__)
#define PM_METAL_NET_HTTP_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_NET_HTTP_WASI_MODULE, name)
#endif

#if !defined(__wasm__)
/* One-time transport init. Returns 0 / -1.
 * impl: common — src/common/pymergetic/metal/net/http.c */
int pm_metal_net_http_init(void);
#endif

/*
 * GET url into dst. Returns HTTP status (>= 100) or -1. out_len optional.
 *
 * impl: common — src/common/pymergetic/metal/net/http.c
 * impl: wasi import — src/common/pymergetic/metal/net/http.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_net_http_get(const char *url, void *dst, size_t dst_cap, size_t *out_len)
	PM_METAL_NET_HTTP_IMPORT(pm_metal_net_http_get);
#else
int pm_metal_net_http_get(const char *url, void *dst, size_t dst_cap, size_t *out_len);
#endif

#if !defined(__wasm__)
/* impl: common — src/common/pymergetic/metal/net/http.c */
int pm_metal_net_http_native_register(void);
#endif

#endif /* PYMERGETIC_METAL_NET_HTTP_H_ */
