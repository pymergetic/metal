/*
 * DHCP bring-up — waits on lwIP dhcp client via Metal async park + net pump.
 */
#include "pymergetic/metal/net/dhcp/__init__.h"

#include <string.h>

#include "lwip/dns.h"
#include "lwip/netif.h"

#include "pymergetic/metal/async/await.h"
#include "pymergetic/metal/async/board_time.h"
#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/async/time.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/cfg.h"
#include "pymergetic/metal/net/ip/lwip_start.h"
#include "pymergetic/metal/net/pump/__init__.h"

#ifndef PM_METAL_DHCP_WAIT_US
#define PM_METAL_DHCP_WAIT_US (15000000ull)
#endif

static uint32_t g_ah;
static uint64_t g_deadline;
static int g_active;
static pm_metal_net_dhcp_lease_t g_lease;

static int dhcp_ready_fill(void)
{
    char ip[16];
    int rc;

    rc = pm_metal_net_ip_if_dhcp_ready("eth0", ip, sizeof(ip));
    if (rc < 0) {
        pm_metal_net_ip_ifcfg_t cfg;
        unsigned n = pm_metal_net_ip_if_count();
        unsigned i;
        rc = -1;
        for (i = 0; i < n; i++) {
            if (pm_metal_net_ip_if_get_index(i, &cfg) != 0) {
                continue;
            }
            if (strcmp(cfg.name, "lo") == 0) {
                continue;
            }
            rc = pm_metal_net_ip_if_dhcp_ready(cfg.name, ip, sizeof(ip));
            break;
        }
        if (i >= n) {
            rc = -1;
        }
    }
    if (rc != 1) {
        return rc;
    }
    memset(&g_lease, 0, sizeof(g_lease));
    g_lease.yiaddr = pm_metal_net_ip_addr();
    g_lease.mask = pm_metal_net_ip_mask();
    g_lease.gw = pm_metal_net_ip_gw();
    g_lease.dns = pm_metal_net_ip_dns();
    if (g_lease.dns == 0u) {
        g_lease.dns = PM_METAL_NET_IP_DEFAULT_DNS;
    }
    return 1;
}

uint32_t pm_metal_net_dhcp_start(void)
{
    if (g_active && g_ah != 0u) {
        return g_ah;
    }
    memset(&g_lease, 0, sizeof(g_lease));
    g_ah = pm_metal_async_park();
    if (g_ah == 0u) {
        return 0;
    }
    g_deadline = pm_metal_time_mono_us() + PM_METAL_DHCP_WAIT_US;
    g_active = 1;
    pm_metal_net_dhcp_poll();
    return g_ah;
}

void pm_metal_net_dhcp_poll(void)
{
    int rc;

    if (!g_active || g_ah == 0u) {
        return;
    }
    if (pm_metal_async_status(g_ah) == PM_METAL_ASYNC_DONE) {
        return;
    }
    rc = dhcp_ready_fill();
    if (rc == 1) {
        pm_metal_async_set_result_u32(g_ah, 1u);
        pm_metal_async_wake(g_ah);
        g_active = 0;
        return;
    }
    if (rc < 0 || pm_metal_time_mono_us() > g_deadline) {
        pm_metal_async_set_result_u32(g_ah, 0u);
        pm_metal_async_wake(g_ah);
        g_active = 0;
    }
}

const pm_metal_net_dhcp_lease_t *pm_metal_net_dhcp_lease(void)
{
    return &g_lease;
}

int32_t pm_metal_net_dhcp_run(pm_metal_net_dhcp_lease_t *out)
{
    uint32_t h;
    uint32_t i;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    h = pm_metal_net_dhcp_start();
    if (h == 0u) {
        return -1;
    }
    for (i = 0; i < 20000u; i++) {
        pm_metal_net_pump_once();
        pm_metal_board_time_advance_us(1000);
        (void)pm_metal_async_run_poll();
        if (pm_metal_async_status(h) == PM_METAL_ASYNC_DONE) {
            break;
        }
    }
    if (pm_metal_async_status(h) != PM_METAL_ASYNC_DONE || pm_metal_async_result_u32(h) != 1u) {
        pm_metal_async_coro_close(h);
        g_ah = 0;
        return -1;
    }
    *out = g_lease;
    pm_metal_async_coro_close(h);
    g_ah = 0;
    return 0;
}
