/* pymergetic.metal.drivers.rtc.sim — two clocks, unbind one. */
#include "pymergetic/metal/drivers.h"
#include "pymergetic/metal/drivers/rtc.h"
#include "pymergetic/metal/drivers/rtc/sim.h"
#include "pymergetic/metal/dt.h"

#include <stdint.h>
#include <stdio.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.rtc.sim test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_rtc_sim_tests(void) {
    int32_t a;
    int32_t b;
    int32_t dt;
    if (pm_metal_drivers_rtc_sim_init(NULL) != -1) {
        return fail("init null");
    }
    a = pm_metal_drivers_rtc_sim_probe(1000);
    b = pm_metal_drivers_rtc_sim_probe(2000);
    if (a < 0 || b < 0 || a == b) {
        return fail("probe two");
    }
    if (pm_metal_drivers_rtc_get(a) != 1000 || pm_metal_drivers_rtc_get(b) != 2000) {
        return fail("get");
    }
    if (pm_metal_drivers_rtc_set(a, 1500) != 0 || pm_metal_drivers_rtc_get(a) != 1500) {
        return fail("set");
    }
    dt = pm_metal_dt_by_class(PM_METAL_DT_CLASS_RTC, 1);
    if (dt < 0 || pm_metal_drivers_unbind(dt) != 0) {
        return fail("unbind");
    }
    if (pm_metal_drivers_rtc_get(a) != 1500) {
        return fail("a after unbind");
    }
    return 0;
}
