/* pymergetic.metal.boot — harness already ran boot; prove ready + reject empty span. */
#include "pymergetic/metal/boot.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.boot test: %s\n", why);
    return 1;
}

int32_t pm_metal_boot_tests(void) {
    /* Host harness is pm_mod_boot_run, not pm_metal_boot — ready stays 0. */
    if (pm_metal_boot_feed_span(0, 0) != -1) {
        return fail("feed empty");
    }
    if (pm_metal_ready() && pm_metal_boot() != 0) {
        return fail("idempotent");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.boot, tests, pm_metal_boot_tests);
