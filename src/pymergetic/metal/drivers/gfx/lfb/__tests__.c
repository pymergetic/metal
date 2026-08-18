/* pymergetic.metal.drivers.gfx.lfb — probe, present, optional PCI match. */
#include "pymergetic/metal/drivers.h"
#include "pymergetic/metal/drivers/gfx.h"
#include "pymergetic/metal/drivers/gfx/lfb.h"
#include "pymergetic/wasmmod/guest.h"


#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.gfx.lfb test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_gfx_lfb_tests(void) {
    int32_t h;
    uint8_t pix[3];
    if (pm_metal_drivers_gfx_lfb_init(NULL) != -1) {
        return fail("init null");
    }
    h = pm_metal_drivers_gfx_lfb_probe();
    if (h < 0) {
        return fail("probe");
    }
    if (pm_metal_drivers_gfx_by_compat("lfb", 0) != h) {
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
    if (pm_metal_drivers_gfx_lfb_up() != 0) {
        return fail("up");
    }
    {
        uint8_t fb[2 * 2 * 3];
        uint8_t src[3];
        int32_t hb;
        memset(fb, 0, sizeof(fb));
        src[0] = 0xaa;
        src[1] = 0xbb;
        src[2] = 0xcc;
        hb = pm_metal_drivers_gfx_lfb_bind(fb, 2, 2, 6);
        if (hb < 0) {
            return fail("bind");
        }
        if (pm_metal_drivers_gfx_present(hb, src, 1, 1, 3) != 0) {
            return fail("bind present");
        }
        if (fb[0] != 0xaa || fb[1] != 0xbb || fb[2] != 0xcc) {
            return fail("lfb copy");
        }
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers.gfx.lfb, tests, pm_metal_drivers_gfx_lfb_tests);
