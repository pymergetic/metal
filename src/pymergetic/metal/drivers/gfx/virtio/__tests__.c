/* pymergetic.metal.drivers.gfx.virtio — probe, present, optional PCI match. */
#include "pymergetic/metal/drivers.h"
#include "pymergetic/metal/drivers/gfx.h"
#include "pymergetic/metal/drivers/gfx/virtio.h"
#include "pymergetic/wasmmod/guest.h"
#include "pymergetic/metal/bus/pci.h"
#include "pymergetic/metal/bus/virtio.h"


#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.gfx.virtio test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_gfx_virtio_tests(void) {
    int32_t h;
    uint8_t pix[3];
    if (pm_metal_drivers_gfx_virtio_init(NULL) != -1) {
        return fail("init null");
    }
    h = pm_metal_drivers_gfx_virtio_probe();
    if (h < 0) {
        return fail("probe");
    }
    if (pm_metal_drivers_gfx_by_compat("virtio-gpu", 0) != h) {
        return fail("compat");
    }
    pix[0] = 0x11;
    pix[1] = 0x22;
    pix[2] = 0x33;
    if (pm_metal_drivers_gfx_present(h, pix, 1, 1, 3) != 0) {
        return fail("present");
    }
    if (pm_metal_drivers_gfx_present_n(h) == 0u) {
        return fail("present_n");
    }
    if (pm_metal_drivers_gfx_virtio_up() != 0) {
        return fail("up");
    }
    {
        int32_t before = pm_metal_drivers_gfx_count();
        if (pm_metal_bus_pci_sim_add(0, 20, 0, PM_METAL_BUS_VIRTIO_VENDOR, PM_METAL_BUS_VIRTIO_DEV_GPU) != 0) {
            return fail("sim add");
        }
        if (pm_metal_drivers_probe() != 0) {
            return fail("probe pci");
        }
        if (pm_metal_drivers_gfx_by_compat("virtio-gpu", 0) < 0) {
            return fail("pci compat");
        }
        if (pm_metal_drivers_gfx_count() < before) {
            return fail("pci count");
        }
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers.gfx.virtio, tests, pm_metal_drivers_gfx_virtio_tests);
