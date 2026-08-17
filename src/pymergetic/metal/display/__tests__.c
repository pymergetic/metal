/* pymergetic.metal.display — attach a 2x2 and put a pixel. */
#include "pymergetic/metal/display.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.display test: %s\n", why);
    return 1;
}

int32_t pm_metal_display_tests(void) {
    uint8_t pix[2 * 2 * 3];
    memset(pix, 0, sizeof(pix));
    if (pm_metal_display_attach(pix, 2, 2, 6) != 0) {
        return fail("attach");
    }
    if (pm_metal_display_width() != 2u || pm_metal_display_height() != 2u) {
        return fail("size");
    }
    if (pm_metal_display_put(1, 0, 0x00aabbccu) != 0) {
        return fail("put");
    }
    if (pm_metal_display_get(1, 0) != 0x00aabbccu) {
        return fail("get");
    }
    if (pm_metal_display_put(2, 0, 1) == 0) {
        return fail("oob");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.display, tests, pm_metal_display_tests);
