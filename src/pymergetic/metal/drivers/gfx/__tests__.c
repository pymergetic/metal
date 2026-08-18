/* pymergetic.metal.drivers.gfx — bind two, present, unbind one. */
#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/gfx.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_last[3];
static uint32_t s_n;

static int32_t nop_open(void *ctx) {
    (void)ctx;
    return 0;
}

static int32_t copy_present(void *ctx, const uint8_t *pix, uint32_t w, uint32_t h, uint32_t stride) {
    (void)ctx;
    if (pix == NULL || w == 0 || h == 0 || stride < w * 3u) {
        return -1;
    }
    memcpy(s_last, pix, 3);
    s_n++;
    return 0;
}

static int32_t nop_poll(void *ctx) {
    (void)ctx;
    return 0;
}

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.gfx test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_gfx_tests(void) {
    pm_metal_gfx_ops_t oa;
    pm_metal_gfx_ops_t ob;
    int32_t da;
    int32_t db;
    int32_t ha;
    int32_t hb;
    int32_t n0;
    uint8_t pix[6];
    memset(&oa, 0, sizeof(oa));
    memset(&ob, 0, sizeof(ob));
    oa.open = nop_open;
    oa.present = copy_present;
    oa.poll = nop_poll;
    ob.open = nop_open;
    ob.present = copy_present;
    ob.poll = nop_poll;
    if (pm_metal_drivers_gfx_init(NULL) != -1) {
        return fail("init null");
    }
    n0 = pm_metal_drivers_gfx_count();
    da = pm_metal_dt_add(PM_METAL_DT_CLASS_GFX, "test-gfx", PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, 0);
    db = pm_metal_dt_add(PM_METAL_DT_CLASS_GFX, "test-gfx", PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, 1);
    ha = pm_metal_drivers_gfx_bind(da, &oa);
    hb = pm_metal_drivers_gfx_bind(db, &ob);
    if (ha < 0 || hb < 0 || ha == hb) {
        return fail("bind two");
    }
    if (pm_metal_drivers_gfx_count() != n0 + 2) {
        return fail("count");
    }
    pix[0] = 0xaa;
    pix[1] = 0xbb;
    pix[2] = 0xcc;
    s_n = 0;
    if (pm_metal_drivers_gfx_present(ha, pix, 1, 1, 3) != 0) {
        return fail("present");
    }
    if (s_n != 1u || s_last[0] != 0xaa || pm_metal_drivers_gfx_present_n(ha) != 1u) {
        return fail("present_n");
    }
    if (pm_metal_drivers_gfx_present(ha, NULL, 1, 1, 3) == 0) {
        return fail("present null");
    }
    if (pm_metal_drivers_gfx_unbind(hb) != 0) {
        return fail("unbind b");
    }
    if (pm_metal_drivers_gfx_count() != n0 + 1) {
        return fail("count after");
    }
    if (pm_metal_drivers_gfx_unbind(ha) != 0) {
        return fail("unbind a");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers.gfx, tests, pm_metal_drivers_gfx_tests);
