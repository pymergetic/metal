/* pymergetic.metal.net.ip — net + cdn faces on the boot.tree surface. */
#include "pymergetic/metal/boot/tree.h"
#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/net/dhcp.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/wasmmod/net/cdn/__exports__.h"

#include <stdio.h>

#ifndef PM_METAL_DT_WALK
#define PM_METAL_DT_WALK 128
#endif

static void emit_cdn(int last, int depth) {
    uint32_t n = pm_wasmmod_net_cdn_base_count();
    uint32_t i;
    char detail[48];
    if (n == 0u) {
        pm_metal_boot_msg_item(last, depth, 1, "cdn", "-");
        return;
    }
    pm_metal_boot_msg_count(detail, sizeof(detail), "ok  ", n, "base");
    pm_metal_boot_msg_item(last, depth, 1, "cdn", detail);
    for (i = 0; i < n; i++) {
        const char *b = pm_wasmmod_net_cdn_base_at(i);
        if (b == NULL || b[0] == 0) {
            b = "?";
        }
        pm_metal_boot_msg_item(i + 1u == n, depth + 1, 1, b, NULL);
    }
}

static const char *addr_how(int32_t h) {
    if (h < 0) {
        return "static";
    }
    if (pm_metal_net_dhcp_leased(h)) {
        return "dhcp";
    }
    return pm_metal_net_dhcp_asked(h) != 0 ? "static  no lease" : "static";
}

static void msg_net(int last) {
    int32_t n = pm_metal_drivers_net_count();
    int32_t i;
    int32_t seen = 0;
    int lo = pm_metal_net_ip_lo_ready() != 0;
    char detail[48];

    (void)last;
    if (n == 0 && !lo) {
        pm_metal_boot_msg_item(0, 0, 0, "net", "-");
        emit_cdn(1, 1);
        return;
    }
    pm_metal_boot_msg_count(detail, sizeof(detail), "ok  ", (unsigned)(n + (int32_t)lo), "interface");
    pm_metal_boot_msg_item(0, 0, 0, "net", detail);
    if (lo) {
        pm_metal_boot_msg_item(0, 1, 1, "lo", "127.0.0.1");
    }
    for (i = 0; i < PM_METAL_DT_WALK && seen < n; i++) {
        int32_t h;
        uint32_t addr;
        uint8_t mac[6];
        char name[48];
        char det[96];
        char traffic[32];
        const char *compat;
        if (pm_metal_dt_class(i) != PM_METAL_DT_CLASS_NET) {
            continue;
        }
        compat = pm_metal_dt_compat(i);
        h = pm_metal_drivers_net_by_dt(i);
        if (h < 0) {
            h = pm_metal_drivers_net_by_compat(compat != NULL ? compat : "", 0);
        }
        addr = h >= 0 ? pm_metal_net_ip_if_addr(h) : 0;
        if (h >= 0) {
            pm_metal_drivers_net_mac(h, mac);
        } else {
            mac[0] = mac[1] = mac[2] = mac[3] = mac[4] = mac[5] = 0;
        }
        snprintf(name, sizeof(name), "%s", compat != NULL ? compat : "net");
        snprintf(traffic, sizeof(traffic), "tx %u/%u  rx %u", (unsigned)pm_metal_drivers_net_tx_n(h),
            (unsigned)(pm_metal_drivers_net_tx_n(h) + pm_metal_drivers_net_tx_err(h)),
            (unsigned)pm_metal_drivers_net_rx_n(h));
        if (addr != 0) {
            snprintf(det, sizeof(det), "%u.%u.%u.%u  %s  %s  %02x:%02x:%02x:%02x:%02x:%02x",
                (unsigned)((addr >> 24) & 0xffu), (unsigned)((addr >> 16) & 0xffu),
                (unsigned)((addr >> 8) & 0xffu), (unsigned)(addr & 0xffu), addr_how(h), traffic,
                (unsigned)mac[0], (unsigned)mac[1], (unsigned)mac[2], (unsigned)mac[3],
                (unsigned)mac[4], (unsigned)mac[5]);
        } else {
            snprintf(det, sizeof(det), "%s  %02x:%02x:%02x:%02x:%02x:%02x", traffic, (unsigned)mac[0],
                (unsigned)mac[1], (unsigned)mac[2], (unsigned)mac[3], (unsigned)mac[4],
                (unsigned)mac[5]);
        }
        pm_metal_boot_msg_item(0, 1, 1, name, det);
        seen++;
    }
    {
        uint32_t gw = pm_metal_net_ip_gw();
        if (gw != 0) {
            char det[32];
            snprintf(det, sizeof(det), "%u.%u.%u.%u", (unsigned)((gw >> 24) & 0xffu),
                (unsigned)((gw >> 16) & 0xffu), (unsigned)((gw >> 8) & 0xffu),
                (unsigned)(gw & 0xffu));
            pm_metal_boot_msg_item(0, 1, 1, "gw", det);
        }
    }
    emit_cdn(1, 1);
}

static void msg_cdn_motd(int last) {
    (void)last;
    emit_cdn(0, 0);
}

PM_METAL_BOOT_MSG_C(PM_METAL_BOOT_SURF_TREE, PM_METAL_BOOT_MSG_NET, msg_net);
PM_METAL_BOOT_MSG_C(PM_METAL_BOOT_SURF_MOTD, PM_METAL_BOOT_MSG_MOTD_CDN, msg_cdn_motd);
