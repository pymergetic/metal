#include "pymergetic/metal/net/pump/__init__.h"

#include "pymergetic/metal/async/await.h"
#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/async/prio.h"
#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/net/asgi/__init__.h"
#include "pymergetic/metal/net/dhcp/__init__.h"
#include "pymergetic/metal/net/http/__init__.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/sock.h"
#include "pymergetic/metal/net/ssh/__init__.h"
#include "pymergetic/metal/net/tls/__init__.h"

#ifndef PM_METAL_NET_TCP_WAITS
#define PM_METAL_NET_TCP_WAITS 16
#endif

enum {
    WAIT_TCP_EST = 1,
    WAIT_TCP_RX = 2
};

typedef struct {
    uint32_t h;
    pm_metal_net_ip_sock_h sock;
    uint32_t min_bytes;
    uint8_t kind;
} tcp_wait_t;

static tcp_wait_t g_waits[PM_METAL_NET_TCP_WAITS];

static int wait_register(uint32_t h, pm_metal_net_ip_sock_h sock, uint8_t kind, uint32_t min_bytes)
{
    uint32_t i;

    for (i = 0; i < PM_METAL_NET_TCP_WAITS; i++) {
        if (g_waits[i].h == 0u) {
            g_waits[i].h = h;
            g_waits[i].sock = sock;
            g_waits[i].kind = kind;
            g_waits[i].min_bytes = min_bytes;
            return 0;
        }
    }
    return -1;
}

void pm_metal_net_pump_wake_tcp(void)
{
    uint32_t i;

    for (i = 0; i < PM_METAL_NET_TCP_WAITS; i++) {
        uint32_t h = g_waits[i].h;
        pm_metal_net_ip_sock_h sock = g_waits[i].sock;
        int ready = 0;

        if (h == 0u || sock == PM_METAL_NET_IP_SOCK_INVALID) {
            continue;
        }
        if (g_waits[i].kind == WAIT_TCP_EST) {
            ready = pm_metal_net_ip_sock_connected(sock) ? 1 : 0;
        } else if (g_waits[i].kind == WAIT_TCP_RX) {
            if (pm_metal_net_ip_sock_rx_avail(sock) >= g_waits[i].min_bytes) {
                ready = 1;
            } else if (pm_metal_net_ip_sock_peer_closed(sock) &&
                       pm_metal_net_ip_sock_rx_avail(sock) > 0u) {
                ready = 1;
            }
        }
        if (ready) {
            pm_metal_async_wake(h);
            g_waits[i].h = 0;
            g_waits[i].sock = PM_METAL_NET_IP_SOCK_INVALID;
            g_waits[i].kind = 0;
            g_waits[i].min_bytes = 0;
        }
    }
}

void pm_metal_net_pump_once(void)
{
    pm_metal_net_ip_poll();
    pm_metal_net_pump_wake_tcp();
    pm_metal_net_dhcp_poll();
    pm_metal_net_tls_poll();
    pm_metal_net_http_client_poll();
    if (pm_metal_asgi_ready()) {
        (void)pm_metal_asgi_poll();
    } else {
        (void)pm_metal_net_http_poll();
    }
    (void)pm_metal_net_ssh_poll();
}

static void pump_tick(void)
{
    pm_metal_net_pump_once();
}

void pm_metal_net_pump_bind_async(void)
{
    pm_metal_async_set_idle_pump(pump_tick);
}

uint32_t pm_metal_net_await_tcp_established_h(pm_metal_net_ip_sock_h sock)
{
    uint32_t h = pm_metal_async_park();

    if (h == 0u || sock == PM_METAL_NET_IP_SOCK_INVALID) {
        return 0;
    }
    pm_metal_async_set_prio(h, PM_METAL_ASYNC_PRIO_HIGH);
    if (wait_register(h, sock, WAIT_TCP_EST, 0) != 0) {
        pm_metal_async_wake(h);
        return 0;
    }
    if (pm_metal_net_ip_sock_connected(sock)) {
        pm_metal_async_wake(h);
        pm_metal_net_pump_wake_tcp();
    }
    return h;
}

uint32_t pm_metal_net_await_tcp_rx_h(pm_metal_net_ip_sock_h sock, uint32_t min_bytes)
{
    uint32_t h = pm_metal_async_park();

    if (h == 0u || sock == PM_METAL_NET_IP_SOCK_INVALID) {
        return 0;
    }
    pm_metal_async_set_prio(h, PM_METAL_ASYNC_PRIO_MED);
    if (wait_register(h, sock, WAIT_TCP_RX, min_bytes) != 0) {
        pm_metal_async_wake(h);
        return 0;
    }
    pm_metal_net_pump_wake_tcp();
    return h;
}

int32_t pm_metal_net_await_tcp_established(pm_metal_net_ip_sock_h sock, uint32_t max_iters)
{
    uint32_t h = pm_metal_net_await_tcp_established_h(sock);
    uint32_t i;

    if (h == 0u) {
        return -1;
    }
    for (i = 0; i < max_iters; i++) {
        (void)pm_metal_async_run_poll();
        if (pm_metal_async_status(h) == PM_METAL_ASYNC_DONE) {
            return 0;
        }
    }
    return -1;
}

int32_t pm_metal_net_await_tcp_rx(pm_metal_net_ip_sock_h sock, uint32_t min_bytes, uint32_t max_iters)
{
    uint32_t h = pm_metal_net_await_tcp_rx_h(sock, min_bytes);
    uint32_t i;

    if (h == 0u) {
        return -1;
    }
    for (i = 0; i < max_iters; i++) {
        (void)pm_metal_async_run_poll();
        if (pm_metal_async_status(h) == PM_METAL_ASYNC_DONE) {
            return 0;
        }
    }
    return -1;
}
