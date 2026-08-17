/* pymergetic.metal.drivers.blk.ide — write/read on ISA-located disk. */
#include "pymergetic/metal/drivers/blk.h"
#include "pymergetic/metal/drivers/blk/ide.h"
#include "pymergetic/metal/dt.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.blk.ide test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_blk_ide_tests(void) {
    int32_t h;
    uint8_t w[512];
    uint8_t r[512];
    int32_t dt;
    if (pm_metal_drivers_blk_ide_init(NULL) != -1) {
        return fail("init null");
    }
    h = pm_metal_drivers_blk_ide_probe(4);
    if (h < 0) {
        return fail("probe");
    }
    dt = pm_metal_drivers_blk_dt_id(h);
    if (dt < 0 || strcmp(pm_metal_dt_compat(dt), "ide-ata") != 0) {
        return fail("dt");
    }
    memset(w, 0x3c, sizeof(w));
    if (pm_metal_drivers_blk_write(h, 0, w, 1) != 0) {
        return fail("write");
    }
    memset(r, 0, sizeof(r));
    if (pm_metal_drivers_blk_read(h, 0, r, 1) != 0 || r[0] != 0x3c) {
        return fail("read");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers.blk.ide, tests, pm_metal_drivers_blk_ide_tests);
