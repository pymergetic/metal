#ifndef PM_METAL_NET_PUMP_H_
#define PM_METAL_NET_PUMP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pump = NIC absorb (prio-agnostic). Bound as async idle_pump so live
 * paths only call pm_metal_async_run_poll() — no outer ip/http/ssh_poll.
 */
void pm_metal_net_pump_once(void);
void pm_metal_net_pump_bind_async(void);

/* Async await handles — complete when condition holds (wake via pump). */
uint32_t pm_metal_net_await_tcp_established_h(void);
uint32_t pm_metal_net_await_tcp_rx_h(uint32_t min_bytes);

/* Sync façades for smoke: run_poll until handle DONE or iters exhausted. */
int32_t pm_metal_net_await_tcp_established(uint32_t max_iters);
int32_t pm_metal_net_await_tcp_rx(uint32_t min_bytes, uint32_t max_iters);

/* Called from pump after ip_poll — wake TCP awaiters into shared prio. */
void pm_metal_net_pump_wake_tcp(void);

#ifdef __cplusplus
}
#endif

#endif
