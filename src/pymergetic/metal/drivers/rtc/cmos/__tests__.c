/* pymergetic.metal.drivers.rtc.cmos — get is host unix time. */
#include "pymergetic/metal/drivers/rtc.h"
#include "pymergetic/metal/drivers/rtc/cmos.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.rtc.cmos test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_rtc_cmos_tests(void) {
    int32_t h;
    int64_t t;
    if (pm_metal_drivers_rtc_cmos_init(NULL) != -1) {
        return fail("init null");
    }
    h = pm_metal_drivers_rtc_cmos_probe();
    if (h < 0) {
        return fail("probe");
    }
    t = pm_metal_drivers_rtc_get(h);
    if (t < 1000000000) {
        return fail("get");
    }
    if (pm_metal_drivers_rtc_cmos_probe() != h) {
        return fail("idempotent");
    }
    (void)time;
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers.rtc.cmos, tests, pm_metal_drivers_rtc_cmos_tests);
