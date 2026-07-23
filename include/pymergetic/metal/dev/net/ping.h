/*
 * ICMP echo (ping) over pm_metal_net_* — IPv4/IPv6 targets via DNS or literal.
 *
 * impl: common — src/pymergetic/metal/dev/net/ping.c
 */
#ifndef PYMERGETIC_METAL_DEV_NET_PING_H_
#define PYMERGETIC_METAL_DEV_NET_PING_H_

#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_NET_PING_WASI_MODULE "pymergetic.metal.net.ping"

#define PM_METAL_NET_PING_ERR_NONE     0u
#define PM_METAL_NET_PING_ERR_RESOLVE  1u
#define PM_METAL_NET_PING_ERR_SEND     2u
#define PM_METAL_NET_PING_ERR_TIMEOUT  3u
#define PM_METAL_NET_PING_ERR_NOROUTE  4u
#define PM_METAL_NET_PING_ERR_NOMEM    5u

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_NET_PING_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_NET_PING_WASI_MODULE, name)

extern pm_metal_async_handle_t pm_metal_net_ping(const char *host,
						 uint32_t timeout_ms)
	PM_METAL_NET_PING_IMPORT(pm_metal_net_ping);
/** After await on pm_metal_net_ping: RTT ms (floored), or 0 on failure/timeout. */
extern uint32_t pm_metal_net_ping_rtt_ms(pm_metal_async_handle_t h)
	PM_METAL_NET_PING_IMPORT(pm_metal_net_ping_rtt_ms);
/** After await: RTT microseconds (0 on failure/timeout). Prefer for display. */
extern uint32_t pm_metal_net_ping_rtt_us(pm_metal_async_handle_t h)
	PM_METAL_NET_PING_IMPORT(pm_metal_net_ping_rtt_us);
/** Last ping failure class (see PM_METAL_NET_PING_ERR_*). */
extern uint32_t pm_metal_net_ping_last_err(void)
	PM_METAL_NET_PING_IMPORT(pm_metal_net_ping_last_err);
#else
pm_metal_async_handle_t pm_metal_net_ping(const char *host, uint32_t timeout_ms);
/** After await: RTT ms (floored), or 0 on failure/timeout. */
uint32_t pm_metal_net_ping_rtt_ms(pm_metal_async_handle_t h);
/** After await: RTT microseconds (0 on failure/timeout). Prefer for display. */
uint32_t pm_metal_net_ping_rtt_us(pm_metal_async_handle_t h);
/** Last ping failure class (see PM_METAL_NET_PING_ERR_*). */
uint32_t pm_metal_net_ping_last_err(void);

int pm_metal_net_ping_native_register(void);
void pm_metal_net_ping_bind_inst(void *module_inst);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_NET_PING_H_ */
