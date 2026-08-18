/* pymergetic.metal.drivers — unbind removes the dt node. */
#include "pymergetic/metal/bus/pci.h"
#include "pymergetic/metal/bus/virtio.h"
#include "pymergetic/metal/drivers.h"
#include "pymergetic/metal/drivers/__types__.h"
#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/blk.h"
#include "pymergetic/metal/drivers/input.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/drivers/rtc.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t nop_open(void *ctx) {
    (void)ctx;
    return 0;
}

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_tests(void) {
    pm_metal_netdev_ops_t ops;
    int32_t dt;
    memset(&ops, 0, sizeof(ops));
    ops.open = nop_open;
    if (pm_metal_drivers_init(NULL) != -1) {
        return fail("init null");
    }
    dt = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "swap-nic", PM_METAL_DT_BUS_PLATFORM, 9, 0, 0, 0);
    if (dt < 0 || pm_metal_drivers_net_bind(dt, &ops) < 0) {
        return fail("bind");
    }
    if (pm_metal_drivers_unbind(dt) != 0) {
        return fail("unbind");
    }
    if (pm_metal_dt_class(dt) != -1) {
        return fail("dt gone");
    }
    return 0;
}

static uint32_t s_late_n;

static int32_t late_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2, uint32_t loc3) {
    (void)loc0;
    (void)loc1;
    (void)loc2;
    (void)loc3;
    if (bus != PM_METAL_DT_BUS_PLATFORM && bus != PM_METAL_DT_BUS_ISA && bus != PM_METAL_DT_BUS_PCI) {
        return -1;
    }
    s_late_n++;
    return 0;
}

int32_t pm_metal_drivers_probe_tests(void) {
    int32_t blk_before;
    if (pm_metal_drivers_probe() != 0) {
        return fail("probe");
    }
    if (pm_metal_drivers_probe() != 0) {
        return fail("probe again");
    }
    if (pm_metal_drivers_net_by_compat("sim", 0) < 0) {
        return fail("sim fill");
    }
    if (pm_metal_drivers_rtc_by_compat("cmos", 0) < 0) {
        return fail("cmos isa");
    }
    if (pm_metal_drivers_input_by_compat("ps2", 0) < 0) {
        return fail("ps2 isa");
    }
    if (pm_metal_bus_pci_sim_add(3, 1, 0, 0x1af4u, 0x1041u) != 0) {
        return fail("pci sim add");
    }
    if (pm_metal_drivers_probe() != 0) {
        return fail("probe pci");
    }
    if (pm_metal_drivers_net_by_compat("virtio-net", 0) < 0) {
        return fail("virtio-net pci match");
    }
    blk_before = pm_metal_drivers_blk_count();
    if (pm_metal_bus_pci_sim_add(3, 2, 0, PM_METAL_BUS_VIRTIO_VENDOR, PM_METAL_BUS_VIRTIO_DEV_BLK)
        != 0) {
        return fail("pci sim blk");
    }
    if (pm_metal_drivers_probe() != 0) {
        return fail("probe blk id");
    }
    if (pm_metal_drivers_blk_count() != blk_before) {
        return fail("host virtio-blk pci is not ramdisk");
    }
    {
        static const pm_metal_drv_t plat = {
            "pymergetic.test.lateplat", PM_METAL_DRV_KIND_PLATFORM, 0, 0, 0, 0, 0, late_attach
        };
        static const pm_metal_drv_t isa = {
            "pymergetic.test.lateisa", PM_METAL_DRV_KIND_ISA, 0x300u, 0, 0, 0, 0, late_attach
        };
        static const pm_metal_drv_t pci = {
            "pymergetic.test.latepci", PM_METAL_DRV_KIND_PCI, 0xbeefu, 0x0001u,
            PM_METAL_DRV_PCI_ANY, PM_METAL_DRV_PCI_ANY, PM_METAL_DRV_PCI_ANY, late_attach
        };
        s_late_n = 0;
        if (pm_metal_drv_add(&plat) != 0 || s_late_n != 1) {
            return fail("late platform");
        }
        if (pm_metal_drv_add(&isa) != 0 || s_late_n != 2) {
            return fail("late isa");
        }
        if (pm_metal_bus_pci_sim_add(4, 0, 0, 0xbeefu, 0x0001u) != 0) {
            return fail("late pci sim");
        }
        if (pm_metal_drv_add(&pci) != 0 || s_late_n != 3) {
            return fail("late pci");
        }
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers, tests, pm_metal_drivers_tests);
PM_MOD_TEST_C(pymergetic.metal.drivers, probe, pm_metal_drivers_probe_tests);
