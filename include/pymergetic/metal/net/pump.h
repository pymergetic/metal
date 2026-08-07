#ifndef PM_METAL_NET_PUMP_H_
#define PM_METAL_NET_PUMP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One NIC+IP poll owner. Protocols should call await helpers or
 * pm_metal_async_run_poll() (after bind) instead of thrashing ip_poll.
 */
void pm_metal_net_pump_once(void);

/* Install pump as async run_poll idle tick (product / live paths). */
void pm_metal_net_pump_bind_async(void);

/* Drive pump via async until TCP focus is ESTABLISHED (or timeout). */
int32_t pm_metal_net_await_tcp_established(uint32_t max_iters);

/* Drive pump via async until ≥min_bytes queued on focused PCB. */
int32_t pm_metal_net_await_tcp_rx(uint32_t min_bytes, uint32_t max_iters);

#ifdef __cplusplus
}
#endif

#endif
