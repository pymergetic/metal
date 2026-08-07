#include "pymergetic/metal/net/pump.h"

#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/tcp.h"

void pm_metal_net_pump_once(void)
{
    pm_metal_net_ip_poll();
}

static void pump_tick(void)
{
    pm_metal_net_pump_once();
}

void pm_metal_net_pump_bind_async(void)
{
    pm_metal_async_set_idle_pump(pump_tick);
}

int32_t pm_metal_net_await_tcp_established(uint32_t max_iters)
{
    uint32_t i;

    for (i = 0; i < max_iters; i++) {
        if (pm_metal_async_ready()) {
            (void)pm_metal_async_run_poll();
        } else {
            pm_metal_net_pump_once();
        }
        if (pm_metal_net_ip_tcp_established()) {
            return 0;
        }
    }
    return -1;
}

int32_t pm_metal_net_await_tcp_rx(uint32_t min_bytes, uint32_t max_iters)
{
    uint32_t i;

    for (i = 0; i < max_iters; i++) {
        if (pm_metal_async_ready()) {
            (void)pm_metal_async_run_poll();
        } else {
            pm_metal_net_pump_once();
        }
        if (pm_metal_net_ip_tcp_rx_avail() >= min_bytes) {
            return 0;
        }
        if (pm_metal_net_ip_tcp_peer_closed()
            && pm_metal_net_ip_tcp_rx_avail() > 0u) {
            return 0;
        }
    }
    return -1;
}
