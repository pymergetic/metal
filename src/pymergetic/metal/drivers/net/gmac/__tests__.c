/* pymergetic.metal.drivers.net.gmac — host has no DWMAC MMIO. */
#include "pymergetic/metal/drivers/net/gmac.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.net.gmac test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_net_gmac_tests(void) {
    if (pm_metal_drivers_net_gmac_init(NULL) != -1) {
        return fail("init null");
    }
    if (pm_metal_drivers_net_gmac_up() != -1) {
        return fail("up without mmio");
    }
    if (pm_metal_drivers_net_gmac_probe() != -1) {
        return fail("probe without mmio");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers.net.gmac, tests, pm_metal_drivers_net_gmac_tests);
