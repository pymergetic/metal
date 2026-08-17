/* pymergetic.metal.drivers.rtc — bind two clocks, set/get, unbind one. */
#include "pymergetic/metal/drivers/rtc.h"
#include "pymergetic/metal/dt.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int64_t s_now;

static int64_t get_now(void *ctx) {
    (void)ctx;
    return s_now;
}

static int32_t set_now(void *ctx, int64_t unix_s) {
    (void)ctx;
    s_now = unix_s;
    return 0;
}

static int64_t get_fixed(void *ctx) {
    (void)ctx;
    return 2000;
}

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.rtc test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_rtc_tests(void) {
    pm_metal_rtc_ops_t oa;
    pm_metal_rtc_ops_t ob;
    int32_t da;
    int32_t db;
    int32_t ha;
    int32_t hb;
    int32_t n0;

    memset(&oa, 0, sizeof(oa));
    memset(&ob, 0, sizeof(ob));
    oa.get = get_now;
    oa.set = set_now;
    ob.get = get_fixed;
    if (pm_metal_drivers_rtc_init(NULL) != -1) {
        return fail("init null");
    }
    n0 = pm_metal_drivers_rtc_count();
    da = pm_metal_dt_add(PM_METAL_DT_CLASS_RTC, "test-rtc", PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, 0);
    db = pm_metal_dt_add(PM_METAL_DT_CLASS_RTC, "test-rtc", PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, 1);
    ha = pm_metal_drivers_rtc_bind(da, &oa);
    hb = pm_metal_drivers_rtc_bind(db, &ob);
    if (ha < 0 || hb < 0 || ha == hb) {
        return fail("bind two");
    }
    if (pm_metal_drivers_rtc_count() != n0 + 2) {
        return fail("count");
    }
    if (pm_metal_drivers_rtc_set(ha, 1500) != 0 || pm_metal_drivers_rtc_get(ha) != 1500) {
        return fail("set/get");
    }
    if (pm_metal_drivers_rtc_get(hb) != 2000) {
        return fail("fixed");
    }
    if (pm_metal_drivers_rtc_unbind(hb) != 0) {
        return fail("unbind b");
    }
    if (pm_metal_drivers_rtc_count() != n0 + 1) {
        return fail("count after");
    }
    if (pm_metal_drivers_rtc_get(ha) != 1500) {
        return fail("a after unbind");
    }
    if (pm_metal_drivers_rtc_unbind(ha) != 0) {
        return fail("unbind a");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers.rtc, tests, pm_metal_drivers_rtc_tests);
