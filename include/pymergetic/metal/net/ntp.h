/*
 * SNTP — Metal net API. Same for guests (wasi import) and host.
 * Uses net/{dns,udp}; no OS headers in the impl.
 */
#ifndef PYMERGETIC_METAL_NET_NTP_H_
#define PYMERGETIC_METAL_NET_NTP_H_

#include <stdint.h>

#include "pymergetic/metal/wasi.h" /* IWYU pragma: keep */

#define PM_METAL_NET_NTP_WASI_MODULE "pymergetic.metal.net.ntp"

#if defined(__wasm__)
#define PM_METAL_NET_NTP_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_NET_NTP_WASI_MODULE, name)
#endif

/*
 * Query server_host via SNTP; write Unix-epoch UTC seconds to *out_unix.
 * NULL/empty host → "pool.ntp.org". timeout_ms <= 0 → 3000. Returns 0/-1.
 *
 * impl: common — src/common/pymergetic/metal/net/ntp.c
 * impl: wasi import — src/common/pymergetic/metal/net/ntp.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_net_ntp_sync(const char *server_host, int32_t timeout_ms, uint64_t *out_unix)
	PM_METAL_NET_NTP_IMPORT(pm_metal_net_ntp_sync);
#else
int pm_metal_net_ntp_sync(const char *server_host, int32_t timeout_ms, uint64_t *out_unix);
#endif

#if !defined(__wasm__)
/* impl: common — src/common/pymergetic/metal/net/ntp.c */
int pm_metal_net_ntp_native_register(void);
#endif

#endif /* PYMERGETIC_METAL_NET_NTP_H_ */
