/* pymergetic.metal.drivers.net.virtio — virtqueue loop UDP (in-process device). */
#include "pymergetic/metal/bus/pci.h"
#include "pymergetic/metal/drivers.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/drivers/net/virtio.h"
#include "pymergetic/metal/net/ip.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VIF4 0x0a010001u

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.net.virtio test: %s\n", why);
    return 1;
}

static int32_t case_mmio(void) {
    uint32_t regs[32];
    memset(regs, 0, sizeof(regs));
    regs[0x000 / 4] = 0x74726976u;
    regs[0x004 / 4] = 2u;
    regs[0x008 / 4] = 1u;
    if (pm_metal_drivers_net_virtio_mmio_up(NULL) != -1) {
        return fail("mmio null");
    }
    regs[0x008 / 4] = 2u;
    if (pm_metal_drivers_net_virtio_mmio_up(regs) != -1) {
        return fail("mmio wrong device");
    }
    regs[0x008 / 4] = 1u;
    if (pm_metal_drivers_net_virtio_mmio_up(regs) != 0) {
        return fail("mmio up");
    }
    if ((regs[0x070 / 4] & (1u | 2u | 4u | 8u)) != (1u | 2u | 4u | 8u)) {
        return fail("mmio status");
    }
    return 0;
}

static int32_t case_pci_bus(void) {
    int32_t before;
    int32_t h0;
    int32_t h1;
    if (pm_metal_bus_pci_sim_add(0, 10, 0, 0x1af4u, 0x1041u) != 0) {
        return fail("sim add 10");
    }
    if (pm_metal_bus_pci_sim_add(0, 11, 0, 0x1af4u, 0x1000u) != 0) {
        return fail("sim add 11");
    }
    before = pm_metal_drivers_net_count();
    if (pm_metal_drivers_probe() != 0) {
        return fail("probe");
    }
    h0 = pm_metal_drivers_net_by_compat("virtio-net", 0);
    h1 = pm_metal_drivers_net_by_compat("virtio-net", 1);
    if (h0 < 0 || h1 < 0 || h0 == h1) {
        return fail("virtio-net pci match");
    }
    if (pm_metal_drivers_net_count() < before + 2) {
        return fail("probe two");
    }
    if (pm_metal_drivers_probe() != 0) {
        return fail("probe idempotent");
    }
    return 0;
}

int32_t pm_metal_drivers_net_virtio_tests(void) {
    if (pm_metal_drivers_net_virtio_init(NULL) != -1) {
        return fail("init null");
    }
    if (case_mmio() != 0) {
        return 1;
    }
    if (case_pci_bus() != 0) {
        return 1;
    }
    if (pm_metal_drivers_net_virtio_up() != 0) {
        return fail("up");
    }
    {
        int32_t h = pm_metal_drivers_net_by_compat("virtio-net", 0);
        if (h < 0 || pm_metal_net_ip_if_up_h(h, VIF4) != 0) {
            return fail("if_up_h");
        }
    }
    int32_t a = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    int32_t b = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    if (a < 0 || b < 0) {
        return fail("socket");
    }
    if (pm_metal_net_ip_bind(a, VIF4, 9301) != 0 || pm_metal_net_ip_bind(b, VIF4, 9302) != 0) {
        return fail("bind");
    }
    const uint8_t msg[] = { 'v', 'q' };
    if (pm_metal_net_ip_sendto(a, msg, sizeof(msg), VIF4, 9302) != 2) {
        return fail("sendto");
    }
    pm_metal_net_ip_pump();
    uint8_t buf[8];
    uint16_t port = 0;
    int32_t n = pm_metal_net_ip_recvfrom(b, buf, sizeof(buf), NULL, &port);
    if (n != 2 || buf[0] != 'v' || port != 9301) {
        (void)pm_metal_net_ip_close(a);
        (void)pm_metal_net_ip_close(b);
        return fail("recvfrom");
    }
    {
        int32_t ha;
        int32_t hb;
        int32_t dt;
        ha = pm_metal_drivers_net_virtio_probe();
        hb = pm_metal_drivers_net_virtio_probe();
        if (ha < 0 || hb < 0 || ha == hb) {
            (void)pm_metal_net_ip_close(a);
            (void)pm_metal_net_ip_close(b);
            return fail("probe two");
        }
        if (pm_metal_drivers_net_count() < 2) {
            (void)pm_metal_net_ip_close(a);
            (void)pm_metal_net_ip_close(b);
            return fail("count");
        }
        if (pm_metal_net_ip_sendto(a, msg, sizeof(msg), VIF4, 9302) != 2) {
            (void)pm_metal_net_ip_close(a);
            (void)pm_metal_net_ip_close(b);
            return fail("sendto 2");
        }
        pm_metal_net_ip_pump();
        n = pm_metal_net_ip_recvfrom(b, buf, sizeof(buf), NULL, &port);
        if (n != 2 || buf[0] != 'v') {
            (void)pm_metal_net_ip_close(a);
            (void)pm_metal_net_ip_close(b);
            return fail("recvfrom 2");
        }
        dt = pm_metal_drivers_net_dt_id(hb);
        if (dt < 0 || pm_metal_drivers_unbind(dt) != 0) {
            (void)pm_metal_net_ip_close(a);
            (void)pm_metal_net_ip_close(b);
            return fail("unbind second");
        }
        if (pm_metal_net_ip_sendto(a, msg, sizeof(msg), VIF4, 9302) != 2) {
            (void)pm_metal_net_ip_close(a);
            (void)pm_metal_net_ip_close(b);
            return fail("sendto after unbind");
        }
        pm_metal_net_ip_pump();
        n = pm_metal_net_ip_recvfrom(b, buf, sizeof(buf), NULL, &port);
        if (n != 2 || buf[0] != 'v' || port != 9301) {
            (void)pm_metal_net_ip_close(a);
            (void)pm_metal_net_ip_close(b);
            return fail("recvfrom after unbind");
        }
    }
    (void)pm_metal_net_ip_close(a);
    (void)pm_metal_net_ip_close(b);
    return 0;
}
