/*
 * DHCP bring-up — waits on lwIP dhcp client (no DIY DORA).
 */
#include "pymergetic/metal/net/dhcp/__init__.h"

#include <string.h>

#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "lwip/dns.h"
#include "lwip/netif.h"

#include "pymergetic/metal/async/board_time.h"
#include "pymergetic/metal/async/time.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/cfg.h"
#include "pymergetic/metal/net/ip/lwip_start.h"

#ifndef PM_METAL_DHCP_WAIT_US
#define PM_METAL_DHCP_WAIT_US (15000000ull)
#endif

int32_t pm_metal_net_dhcp_run(pm_metal_net_dhcp_lease_t *out)
{
    char ip[16];
    uint64_t start = pm_metal_time_mono_us();
    int rc;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    for (;;) {
        pm_metal_net_ip_poll();
        /* Smoke path has no async idle pump — advance board clock for lwIP timers. */
        pm_metal_board_time_advance_us(1000);
        rc = pm_metal_net_ip_if_dhcp_ready("eth0", ip, sizeof(ip));
        if (rc < 0) {
            /* No eth0 yet — try first non-lo iface */
            pm_metal_net_ip_ifcfg_t cfg;
            unsigned n = pm_metal_net_ip_if_count();
            unsigned i;
            rc = 0;
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
        if (rc == 1) {
            out->yiaddr = pm_metal_net_ip_addr();
            out->mask = pm_metal_net_ip_mask();
            out->gw = pm_metal_net_ip_gw();
            out->dns = pm_metal_net_ip_dns();
            if (out->dns == 0u) {
                out->dns = PM_METAL_NET_IP_DEFAULT_DNS;
            }
            return 0;
        }
        if (rc < 0) {
            return -1;
        }
        if (pm_metal_time_mono_us() - start > PM_METAL_DHCP_WAIT_US) {
            return -1;
        }
    }
}
