/* pymergetic.metal.drivers.blk — bind two, write/read, unbind one. */
#include "pymergetic/metal/drivers/blk.h"
#include "pymergetic/metal/dt.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_sec[512];

static int32_t ready_ok(void *ctx) {
    (void)ctx;
    return 1;
}

static uint64_t cap_one(void *ctx) {
    (void)ctx;
    return 1;
}

static int32_t rd(void *ctx, uint64_t lba, void *buf, uint32_t nsec) {
    (void)ctx;
    (void)lba;
    if (buf == NULL || nsec == 0) {
        return -1;
    }
    memcpy(buf, s_sec, 512);
    return 0;
}

static int32_t wr(void *ctx, uint64_t lba, const void *buf, uint32_t nsec) {
    (void)ctx;
    (void)lba;
    if (buf == NULL || nsec == 0) {
        return -1;
    }
    memcpy(s_sec, buf, 512);
    return 0;
}

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.blk test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_blk_tests(void) {
    pm_metal_blk_ops_t oa;
    pm_metal_blk_ops_t ob;
    int32_t da;
    int32_t db;
    int32_t ha;
    int32_t hb;
    int32_t n0;
    uint8_t w[512];
    uint8_t r[512];

    memset(&oa, 0, sizeof(oa));
    memset(&ob, 0, sizeof(ob));
    oa.ready = ready_ok;
    oa.capacity = cap_one;
    oa.read = rd;
    oa.write = wr;
    ob.ready = ready_ok;
    ob.capacity = cap_one;
    if (pm_metal_drivers_blk_init(NULL) != -1) {
        return fail("init null");
    }
    n0 = pm_metal_drivers_blk_count();
    da = pm_metal_dt_add(PM_METAL_DT_CLASS_BLK, "test-blk", PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, 0);
    db = pm_metal_dt_add(PM_METAL_DT_CLASS_BLK, "test-blk", PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, 1);
    ha = pm_metal_drivers_blk_bind(da, &oa);
    hb = pm_metal_drivers_blk_bind(db, &ob);
    if (ha < 0 || hb < 0 || ha == hb) {
        return fail("bind two");
    }
    if (pm_metal_drivers_blk_count() != n0 + 2) {
        return fail("count");
    }
    if (!pm_metal_drivers_blk_ready(ha) || pm_metal_drivers_blk_capacity(ha) != 1) {
        return fail("ready/cap");
    }
    memset(w, 0xa5, sizeof(w));
    if (pm_metal_drivers_blk_write(ha, 0, w, 1) != 0) {
        return fail("write");
    }
    memset(r, 0, sizeof(r));
    if (pm_metal_drivers_blk_read(ha, 0, r, 1) != 0 || r[0] != 0xa5) {
        return fail("read");
    }
    if (pm_metal_drivers_blk_unbind(hb) != 0) {
        return fail("unbind b");
    }
    if (pm_metal_drivers_blk_count() != n0 + 1) {
        return fail("count after");
    }
    if (pm_metal_drivers_blk_unbind(ha) != 0) {
        return fail("unbind a");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers.blk, tests, pm_metal_drivers_blk_tests);
