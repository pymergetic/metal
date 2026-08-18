/* pymergetic.metal.drivers.input.virtio — probe, inject, optional PCI match. */
#include "pymergetic/metal/bus/pci.h"
#include "pymergetic/metal/bus/virtio.h"
#include "pymergetic/metal/drivers.h"
#include "pymergetic/metal/drivers/input.h"
#include "pymergetic/metal/drivers/input/virtio.h"
#include "pymergetic/metal/input.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.input.virtio test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_input_virtio_tests(void) {
    int32_t h;
    if (pm_metal_drivers_input_virtio_init(NULL) != -1) {
        return fail("init null");
    }
    h = pm_metal_drivers_input_virtio_probe();
    if (h < 0) {
        return fail("probe");
    }
    if (pm_metal_drivers_input_by_compat("virtio-input", 0) != h) {
        return fail("compat");
    }
    if (pm_metal_drivers_input_inject(h, (int32_t)'K') != 0) {
        return fail("inject");
    }
    if (pm_metal_input_pop() != (int32_t)'K') {
        return fail("pop");
    }
    if (pm_metal_drivers_input_virtio_event(PM_METAL_VIRTIO_INPUT_EV_KEY,
            PM_METAL_VIRTIO_INPUT_KEY_A, 1)
        != 0) {
        return fail("event a");
    }
    if (pm_metal_input_pop() != (int32_t)'a') {
        return fail("event pop");
    }
    if (pm_metal_drivers_input_virtio_event(PM_METAL_VIRTIO_INPUT_EV_KEY,
            PM_METAL_VIRTIO_INPUT_KEY_F1, 1)
        != 0) {
        return fail("event f1");
    }
    if (pm_metal_input_pop() != PM_METAL_INPUT_KEY_F1) {
        return fail("event f1 pop");
    }
    if (pm_metal_drivers_input_virtio_event(PM_METAL_VIRTIO_INPUT_EV_ABS,
            PM_METAL_VIRTIO_INPUT_ABS_X, 320)
        != 0
        || pm_metal_drivers_input_virtio_event(PM_METAL_VIRTIO_INPUT_EV_ABS,
            PM_METAL_VIRTIO_INPUT_ABS_Y, 200)
            != 0) {
        return fail("abs");
    }
    {
        int32_t x = 0;
        int32_t y = 0;
        if (pm_metal_drivers_input_virtio_abs(&x, &y) != 0 || x != 320 || y != 200) {
            return fail("abs get");
        }
    }
    if (pm_metal_drivers_input_virtio_event(PM_METAL_VIRTIO_INPUT_EV_KEY,
            PM_METAL_VIRTIO_INPUT_BTN_TOUCH, 1)
        != 0) {
        return fail("touch");
    }
    if (pm_metal_drivers_input_virtio_up() != 0) {
        return fail("up");
    }
    {
        int32_t before = pm_metal_drivers_input_count();
        if (pm_metal_bus_pci_sim_add(0, 21, 0, PM_METAL_BUS_VIRTIO_VENDOR,
                PM_METAL_BUS_VIRTIO_DEV_INPUT)
            != 0) {
            return fail("sim add");
        }
        if (pm_metal_drivers_probe() != 0) {
            return fail("probe pci");
        }
        if (pm_metal_drivers_input_by_compat("virtio-input", 0) < 0) {
            return fail("pci compat");
        }
        if (pm_metal_drivers_input_count() < before) {
            return fail("pci count");
        }
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers.input.virtio, tests, pm_metal_drivers_input_virtio_tests);
