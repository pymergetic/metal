/* pymergetic.metal.drivers.blk.virtio — two disks, unbind one. */
#include "pymergetic/metal/drivers.h"
#include "pymergetic/metal/drivers/blk.h"
#include "pymergetic/metal/drivers/blk/virtio.h"
#include "pymergetic/metal/dt.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.blk.virtio test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_blk_virtio_tests(void) {
    int32_t a;
    int32_t b;
    uint8_t w[512];
    uint8_t r[512];
    int32_t dt;
    if (pm_metal_drivers_blk_virtio_init(NULL) != -1) {
        return fail("init null");
    }
    a = pm_metal_drivers_blk_virtio_probe(8);
    b = pm_metal_drivers_blk_virtio_probe(8);
    if (a < 0 || b < 0 || a == b) {
        return fail("probe two");
    }
    if (pm_metal_drivers_blk_count() < 2) {
        return fail("count");
    }
    memset(w, 0xa5, sizeof(w));
    if (pm_metal_drivers_blk_write(a, 1, w, 1) != 0) {
        return fail("write a");
    }
    memset(r, 0, sizeof(r));
    if (pm_metal_drivers_blk_read(a, 1, r, 1) != 0 || r[0] != 0xa5) {
        return fail("read a");
    }
    dt = pm_metal_drivers_blk_dt_id(b);
    if (dt < 0 || pm_metal_drivers_unbind(dt) != 0) {
        return fail("unbind b");
    }
    memset(r, 0, sizeof(r));
    if (pm_metal_drivers_blk_read(a, 1, r, 1) != 0 || r[0] != 0xa5) {
        return fail("a after unbind");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers.blk.virtio, tests, pm_metal_drivers_blk_virtio_tests);
