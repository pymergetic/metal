/*
 * SNTP client — fetch wall-clock UTC seconds (Unix epoch) from an NTP
 * server over UDP. Host-side only implementation
 * (src/common/pymergetic/metal/util/ntp.c); wasm32 mods reach it via this
 * module's wasi-style import bridge (same pattern as util/lz4.h).
 *
 * Real socket I/O is gated by PM_METAL_HAVE_NET (linux today); other
 * targets stub sync() to -1 until their port grows a net stack. HTTP
 * Date / time-URL fallback lands later on top of util/http.h.
 */
#ifndef PYMERGETIC_METAL_UTIL_NTP_H_
#define PYMERGETIC_METAL_UTIL_NTP_H_

#include <stdint.h>

#include "pymergetic/metal/util/wasi.h" /* IWYU pragma: keep */

#define PM_METAL_UTIL_NTP_WASI_MODULE "pymergetic.metal.util.ntp"

#if defined(__wasm__)
#define PM_METAL_UTIL_NTP_IMPORT(name) \
	PM_METAL_UTIL_WASI_IMPORT(PM_METAL_UTIL_NTP_WASI_MODULE, name)
#endif

/*
 * Query server_host (NUL-terminated DNS name or dotted IPv4) via SNTP
 * and write Unix-epoch UTC seconds into *out_unix. server_host NULL or
 * empty uses "pool.ntp.org". timeout_ms <= 0 means 3000. Returns 0 on
 * success, -1 on resolve/send/recv/parse failure or when net is stubbed.
 *
 * impl: common — src/common/pymergetic/metal/util/ntp.c
 * impl: wasi import — src/common/pymergetic/metal/util/ntp.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_ntp_sync(const char *server_host, int32_t timeout_ms, uint64_t *out_unix)
	PM_METAL_UTIL_NTP_IMPORT(pm_metal_util_ntp_sync);
#else
int pm_metal_util_ntp_sync(const char *server_host, int32_t timeout_ms, uint64_t *out_unix);
#endif

#if !defined(__wasm__)
/*
 * Registers this module's wasi-style imports. Call once from runtime init
 * after wasm_runtime_full_init(), before any load that might import them.
 *
 * impl: common — src/common/pymergetic/metal/util/ntp.c
 */
int pm_metal_util_ntp_native_register(void);
#endif

#endif /* PYMERGETIC_METAL_UTIL_NTP_H_ */
