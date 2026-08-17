/* pymergetic.metal.drivers.net.bge — BAR magic, PCI find, probe two. */
#include "pymergetic/metal/bus/pci.h"
#include "pymergetic/metal/drivers.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/drivers/net/bge.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BIF4 0x0a020001u

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.net.bge test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_net_bge_tests(void) {
    uint32_t regs[8];
    int32_t ha;
    int32_t hb;
    int32_t dt;
    int32_t before;
    if (pm_metal_drivers_net_bge_init(NULL) != -1) {
        return fail("init null");
    }
    if (pm_metal_drivers_net_bge_mmio_up(NULL) != -1) {
        return fail("mmio null");
    }
    memset(regs, 0, sizeof(regs));
    if (pm_metal_drivers_net_bge_mmio_up(regs) != -1) {
        return fail("mmio no vendor");
    }
    regs[0] = 0x14e4u;
    if (pm_metal_drivers_net_bge_mmio_up(regs) != 0) {
        return fail("mmio up");
    }
    {
        int32_t h = pm_metal_drivers_net_by_compat("bge", 0);
        if (h < 0 || pm_metal_net_ip_if_up_h(h, BIF4) != 0) {
            return fail("if_up_h");
        }
    }
    {
        int32_t a = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
        int32_t b = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
        uint8_t buf[8];
        uint16_t port = 0;
        const uint8_t msg[] = { 'b', 'g' };
        if (a < 0 || b < 0) {
            return fail("socket");
        }
        if (pm_metal_net_ip_bind(a, BIF4, 9401) != 0 || pm_metal_net_ip_bind(b, BIF4, 9402) != 0) {
            return fail("bind");
        }
        if (pm_metal_net_ip_sendto(a, msg, sizeof(msg), BIF4, 9402) != 2) {
            return fail("sendto");
        }
        pm_metal_net_ip_pump();
        if (pm_metal_net_ip_recvfrom(b, buf, sizeof(buf), NULL, &port) != 2 || buf[0] != 'b'
            || port != 9401) {
            return fail("recvfrom");
        }
        (void)pm_metal_net_ip_close(a);
        (void)pm_metal_net_ip_close(b);
    }
    if (pm_metal_bus_pci_sim_add(0, 14, 0, 0x14e4u, 0x16c7u) != 0
        || pm_metal_bus_pci_sim_add(0, 15, 0, 0x14e4u, 0x1647u) != 0) {
        return fail("sim add");
    }
    before = pm_metal_drivers_net_count();
    if (pm_metal_drivers_probe() != 0) {
        return fail("probe pci");
    }
    if (pm_metal_drivers_net_by_compat("bge", 0) < 0 || pm_metal_drivers_net_by_compat("bge", 1) < 0) {
        return fail("bge pci match");
    }
    if (pm_metal_drivers_net_count() < before + 2) {
        return fail("probe two");
    }
    ha = pm_metal_drivers_net_bge_probe();
    hb = pm_metal_drivers_net_bge_probe();
    if (ha < 0 || hb < 0 || ha == hb) {
        return fail("probe two");
    }
    dt = pm_metal_drivers_net_dt_id(hb);
    if (dt < 0 || pm_metal_drivers_unbind(dt) != 0) {
        return fail("unbind second");
    }
    if (pm_metal_net_ip_if_up_h(ha, BIF4) != 0) {
        return fail("if_up_h");
    }
    {
        int32_t a = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
        int32_t b = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
        uint8_t buf[8];
        uint16_t port = 0;
        const uint8_t msg[] = { 'b', '2' };
        if (a < 0 || b < 0 || pm_metal_net_ip_bind(a, BIF4, 9411) != 0
            || pm_metal_net_ip_bind(b, BIF4, 9412) != 0) {
            return fail("probe udp bind");
        }
        if (pm_metal_net_ip_sendto(a, msg, sizeof(msg), BIF4, 9412) != 2) {
            return fail("probe sendto");
        }
        pm_metal_net_ip_pump();
        if (pm_metal_net_ip_recvfrom(b, buf, sizeof(buf), NULL, &port) != 2 || buf[0] != 'b') {
            return fail("probe recvfrom");
        }
        (void)pm_metal_net_ip_close(a);
        (void)pm_metal_net_ip_close(b);
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers.net.bge, tests, pm_metal_drivers_net_bge_tests);
