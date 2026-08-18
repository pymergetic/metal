/* pymergetic.metal.drivers.input.ps2 — probe, inject, idempotent. */
#include "pymergetic/metal/drivers/input.h"
#include "pymergetic/metal/drivers/input/ps2.h"
#include "pymergetic/metal/input.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.input.ps2 test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_input_ps2_tests(void) {
    int32_t h;
    if (pm_metal_drivers_input_ps2_init(NULL) != -1) {
        return fail("init null");
    }
    h = pm_metal_drivers_input_ps2_probe();
    if (h < 0) {
        return fail("probe");
    }
    if (pm_metal_drivers_input_by_compat("ps2", 0) != h) {
        return fail("compat");
    }
    if (pm_metal_drivers_input_inject(h, (int32_t)'P') != 0) {
        return fail("inject");
    }
    if (pm_metal_input_pop() != (int32_t)'P') {
        return fail("pop");
    }
    if (pm_metal_drivers_input_ps2_probe() != h) {
        return fail("idempotent");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers.input.ps2, tests, pm_metal_drivers_input_ps2_tests);
