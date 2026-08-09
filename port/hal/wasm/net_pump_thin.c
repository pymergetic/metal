/*
 * Browser net.pump — thin idle pump over browser http client (no lwIP/L2).
 */
#include "pymergetic/metal/net/pump/__init__.h"
#include "pymergetic/metal/net/http/__init__.h"
#include "pymergetic/metal/net/dhcp/__init__.h"
#include "pymergetic/metal/net/ntp/__init__.h"
#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/async/handle.h"

void pm_metal_net_pump_once(void)
{
    pm_metal_net_http_client_poll();
    pm_metal_net_dhcp_poll();
    pm_metal_net_ntp_poll();
}

void pm_metal_net_pump_bind_async(void)
{
    pm_metal_async_set_idle_pump(pm_metal_net_pump_once);
}

uint32_t pm_metal_net_await_tcp_established_h(pm_metal_net_ip_sock_h sock)
{
    (void)sock;
    return pm_metal_async_completed_u32(0u);
}

uint32_t pm_metal_net_await_tcp_rx_h(pm_metal_net_ip_sock_h sock, uint32_t min_bytes)
{
    (void)sock;
    (void)min_bytes;
    return pm_metal_async_completed_u32(0u);
}

int32_t pm_metal_net_await_tcp_established(pm_metal_net_ip_sock_h sock, uint32_t max_iters)
{
    (void)sock;
    (void)max_iters;
    return -1;
}

int32_t pm_metal_net_await_tcp_rx(pm_metal_net_ip_sock_h sock, uint32_t min_bytes,
                                  uint32_t max_iters)
{
    (void)sock;
    (void)min_bytes;
    (void)max_iters;
    return -1;
}

void pm_metal_net_pump_wake_tcp(void) {}
