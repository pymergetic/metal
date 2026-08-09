/*
 * Browser net.dhcp — same C ABI; synthetic lease (no UDP DHCP wire).
 * Defaults mirror QEMU user-net guest identity used by net.ip defaults.
 */
#include "pymergetic/metal/net/dhcp/__init__.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/async/handle.h"

#include <string.h>

static pm_metal_net_dhcp_lease_t g_lease;
static int g_have_lease;

static void fill_default_lease(void)
{
    g_lease.yiaddr = PM_METAL_NET_IP_DEFAULT_ADDR;
    g_lease.mask = PM_METAL_NET_IP_DEFAULT_MASK;
    g_lease.gw = PM_METAL_NET_IP_DEFAULT_GW;
    g_lease.dns = PM_METAL_NET_IP_DEFAULT_DNS;
    g_lease.server = PM_METAL_NET_IP_DEFAULT_GW;
    g_have_lease = 1;
}

uint32_t pm_metal_net_dhcp_start(void)
{
    fill_default_lease();
    (void)pm_metal_net_ip_set_addrs(g_lease.yiaddr, g_lease.mask, g_lease.gw);
    (void)pm_metal_net_ip_set_dns(g_lease.dns);
    return pm_metal_async_completed_u32(1u);
}

void pm_metal_net_dhcp_poll(void) {}

const pm_metal_net_dhcp_lease_t *pm_metal_net_dhcp_lease(void)
{
    if (!g_have_lease) {
        fill_default_lease();
    }
    return &g_lease;
}

int32_t pm_metal_net_dhcp_run(pm_metal_net_dhcp_lease_t *lease_out)
{
    uint32_t h = pm_metal_net_dhcp_start();

    if (pm_metal_async_result_u32(h) == 0u) {
        return -1;
    }
    if (lease_out) {
        *lease_out = g_lease;
    }
    return 0;
}
