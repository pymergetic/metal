/*
 * SNTP client — guest/host dual ABI (UDP/123).
 *
 * Async sync. After await on the returned handle:
 *   pm_metal_net_ntp_status(h) — 0 on success, else error
 *   pm_metal_net_ntp_last_unix_ms() — UTC epoch ms from last success
 *
 * On success the host wall clock is updated via pm_metal_realtime_set_unix_ms.
 * host may be a hostname, dotted IPv4, or NULL to use DHCP option 42 from the
 * default interface when present.
 *
 * impl: common — src/pymergetic/metal/dev/net/ntp.c
 */
#ifndef PYMERGETIC_METAL_DEV_NET_NTP_H_
#define PYMERGETIC_METAL_DEV_NET_NTP_H_

#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_NET_NTP_WASI_MODULE "pymergetic.metal.net.ntp"

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_NET_NTP_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_NET_NTP_WASI_MODULE, name)

extern pm_metal_async_handle_t pm_metal_net_ntp_sync(const char *host)
	PM_METAL_NET_NTP_IMPORT(pm_metal_net_ntp_sync);
extern uint32_t pm_metal_net_ntp_status(pm_metal_async_handle_t h)
	PM_METAL_NET_NTP_IMPORT(pm_metal_net_ntp_status);
extern uint64_t pm_metal_net_ntp_last_unix_ms(void)
	PM_METAL_NET_NTP_IMPORT(pm_metal_net_ntp_last_unix_ms);
#else
pm_metal_async_handle_t pm_metal_net_ntp_sync(const char *host);
uint32_t pm_metal_net_ntp_status(pm_metal_async_handle_t h);
uint64_t pm_metal_net_ntp_last_unix_ms(void);

int pm_metal_net_ntp_native_register(void);
void pm_metal_net_ntp_bind_inst(void *module_inst);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_NET_NTP_H_ */
