/*
 * Browser net.ip — ifcfg face only (no L2/L4). Synthetic addrs for nest+import.
 * Socks remain unavailable; ARP/ping are no-ops / fail-closed.
 */
#include "pymergetic/metal/net/ip/__init__.h"

#include <stdint.h>

static int g_ready;
static uint32_t g_addr = PM_METAL_NET_IP_DEFAULT_ADDR;
static uint32_t g_mask = PM_METAL_NET_IP_DEFAULT_MASK;
static uint32_t g_gw = PM_METAL_NET_IP_DEFAULT_GW;
static uint32_t g_dns = PM_METAL_NET_IP_DEFAULT_DNS;

int32_t pm_metal_net_ip_init(uint32_t addr_be, uint32_t mask_be, uint32_t gw_be)
{
    if (addr_be) {
        g_addr = addr_be;
    }
    if (mask_be) {
        g_mask = mask_be;
    }
    if (gw_be) {
        g_gw = gw_be;
    }
    g_ready = 1;
    return 0;
}

int32_t pm_metal_net_ip_ready(void)
{
    return g_ready ? 1 : 0;
}

int32_t pm_metal_net_ip_set_addrs(uint32_t addr, uint32_t mask, uint32_t gw)
{
    g_addr = addr;
    g_mask = mask;
    g_gw = gw;
    g_ready = 1;
    return 0;
}

int32_t pm_metal_net_ip_set_dns(uint32_t dns)
{
    g_dns = dns;
    return 0;
}

uint32_t pm_metal_net_ip_addr(void)
{
    return g_addr;
}

uint32_t pm_metal_net_ip_gw(void)
{
    return g_gw;
}

uint32_t pm_metal_net_ip_mask(void)
{
    return g_mask;
}

uint32_t pm_metal_net_ip_dns(void)
{
    return g_dns;
}

int32_t pm_metal_net_ip_arp_resolve(uint32_t ip_host)
{
    (void)ip_host;
    return -1;
}

int32_t pm_metal_net_ip_announce(void)
{
    return g_ready ? 0 : -1;
}

void pm_metal_net_ip_poll(void) {}

int32_t pm_metal_net_ip_ping(uint32_t dst_ip, uint16_t id, uint16_t seq)
{
    (void)dst_ip;
    (void)id;
    (void)seq;
    return -1;
}

uint32_t pm_metal_net_ip_ping_replies(void)
{
    return 0u;
}
