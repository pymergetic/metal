/* pymergetic.metal.drivers.net.tap — probe/unbind two; tun wire is optional. */
#include "pymergetic/metal/drivers.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/drivers/net/tap.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.net.tap test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_net_tap_tests(void) {
    int32_t ha;
    int32_t hb;
    int32_t dt;
    if (pm_metal_drivers_net_tap_init(NULL) != -1) {
        return fail("init null");
    }
    ha = pm_metal_drivers_net_tap_probe();
    hb = pm_metal_drivers_net_tap_probe();
    if (ha < 0 || hb < 0 || ha == hb) {
        return fail("probe two");
    }
    if (pm_metal_drivers_net_count() < 2) {
        return fail("count");
    }
    dt = pm_metal_drivers_net_dt_id(hb);
    if (dt < 0 || pm_metal_drivers_unbind(dt) != 0) {
        return fail("unbind second");
    }
    if (pm_metal_drivers_net_dt_id(ha) < 0) {
        return fail("first gone");
    }
    /* Real /dev/net/tun needs CAP_NET_ADMIN; fd stays -1 without it. */
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers.net.tap, tests, pm_metal_drivers_net_tap_tests);
