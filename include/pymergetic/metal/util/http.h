/*
 * HTTP(S) client — thin wrapper around vendored libcurl
 * (external/curl + mbedTLS + nghttp2; see scripts/setup-net.sh).
 * Host-side only (src/common/pymergetic/metal/util/http.c); wasm32 mods
 * call through this module's wasi-style import bridge (same pattern as
 * util/lz4.h). Guests never link curl/mbedTLS/nghttp2 themselves.
 *
 * Real curl I/O is gated by PM_METAL_HAVE_NET (linux today); other
 * targets stub get() to -1 until their port wires the same stack.
 */
#ifndef PYMERGETIC_METAL_UTIL_HTTP_H_
#define PYMERGETIC_METAL_UTIL_HTTP_H_

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/util/wasi.h" /* IWYU pragma: keep */

#define PM_METAL_UTIL_HTTP_WASI_MODULE "pymergetic.metal.util.http"

#if defined(__wasm__)
#define PM_METAL_UTIL_HTTP_IMPORT(name) \
	PM_METAL_UTIL_WASI_IMPORT(PM_METAL_UTIL_HTTP_WASI_MODULE, name)
#endif

/*
 * HTTP/HTTPS GET url into dst (capacity dst_cap). On success writes the
 * response body (truncated to dst_cap), sets *out_len to the number of
 * body bytes stored, and returns the HTTP status code (>= 100). Returns
 * -1 on transport/TLS/URL errors or when net is stubbed. out_len may be
 * NULL. Follows redirects (curl default). Prefer HTTPS; plain http://
 * is allowed when the host policy does.
 *
 * impl: common — src/common/pymergetic/metal/util/http.c
 * impl: wasi import — src/common/pymergetic/metal/util/http.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_http_get(const char *url, void *dst, size_t dst_cap, size_t *out_len)
	PM_METAL_UTIL_HTTP_IMPORT(pm_metal_util_http_get);
#else
int pm_metal_util_http_get(const char *url, void *dst, size_t dst_cap, size_t *out_len);
#endif

#if !defined(__wasm__)
/*
 * Registers this module's wasi-style imports and runs curl_global_init()
 * when PM_METAL_HAVE_NET is set. Call once from runtime init.
 *
 * impl: common — src/common/pymergetic/metal/util/http.c
 */
int pm_metal_util_http_native_register(void);
#endif

#endif /* PYMERGETIC_METAL_UTIL_HTTP_H_ */
