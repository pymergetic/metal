/* pymergetic.metal.bus.pci — two identical IDs at different slots. */
#include "pymergetic/metal/bus/pci.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.bus.pci test: %s\n", why);
    return 1;
}

int32_t pm_metal_bus_pci_tests(void) {
    uint32_t b0 = 99;
    uint32_t d0 = 99;
    uint32_t f0 = 99;
    uint32_t b1 = 99;
    uint32_t d1 = 99;
    uint32_t f1 = 99;
    if (pm_metal_bus_pci_init(NULL) != -1) {
        return fail("init null");
    }
    if (pm_metal_bus_pci_sim_add(0, 3, 0, 0x1af4u, 0x1041u) != 0) {
        return fail("sim 0");
    }
    if (pm_metal_bus_pci_sim_add(0, 4, 0, 0x1af4u, 0x1041u) != 0) {
        return fail("sim 1");
    }
    if (pm_metal_bus_pci_find_nth(0x1af4u, 0x1041u, 0, &b0, &d0, &f0) != 0 || d0 != 3u) {
        return fail("find 0");
    }
    if (pm_metal_bus_pci_find_nth(0x1af4u, 0x1041u, 1, &b1, &d1, &f1) != 0 || d1 != 4u) {
        return fail("find 1");
    }
    if (pm_metal_bus_pci_find_nth(0x1af4u, 0x1041u, 2, NULL, NULL, NULL) != -1) {
        return fail("find 2");
    }
    if (pm_metal_bus_pci_cfg_read32(0, 3, 0, 0) != (0x1af4u | (0x1041u << 16))) {
        return fail("cfg");
    }
    if (pm_metal_bus_pci_cfg_read32(0, 3, 0, 0x0cu) != 0) {
        return fail("header type");
    }
    {
        uint32_t device = 0;
        if (pm_metal_bus_pci_sim_add(0, 6, 0, 0x14e4u, 0x16c7u) != 0) {
            return fail("sim bge");
        }
        if (pm_metal_bus_pci_find_vendor_nth(0x14e4u, 0, &b0, &d0, &f0, &device) != 0 || d0 != 6u
            || device != 0x16c7u) {
            return fail("vendor nth");
        }
        if (pm_metal_bus_pci_find_vendor_nth(0x14e4u, 1, NULL, NULL, NULL, NULL) != -1) {
            return fail("vendor nth 1");
        }
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.bus.pci, tests, pm_metal_bus_pci_tests);
