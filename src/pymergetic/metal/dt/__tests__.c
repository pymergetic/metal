/* pymergetic.metal.dt — two of the same compatible, unbind one. */
#include "pymergetic/metal/dt.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.dt test: %s\n", why);
    return 1;
}

int32_t pm_metal_dt_tests(void) {
    int32_t a;
    int32_t b;
    int32_t again;
    int32_t n0;
    int32_t n;
    int32_t i;
    int found_a;
    int found_b;
    if (pm_metal_dt_init(NULL) != -1) {
        return fail("init null");
    }
    n0 = pm_metal_dt_count_class(PM_METAL_DT_CLASS_NET);
    a = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "metal-dt-test", PM_METAL_DT_BUS_PCI, 0, 1, 0, 0);
    b = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "metal-dt-test", PM_METAL_DT_BUS_PCI, 0, 2, 0, 0);
    if (a < 0 || b < 0 || a == b) {
        return fail("add two");
    }
    again = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "metal-dt-test", PM_METAL_DT_BUS_PCI, 0, 1, 0, 0);
    if (again != a) {
        return fail("idempotent");
    }
    if (pm_metal_dt_count_class(PM_METAL_DT_CLASS_NET) != n0 + 2) {
        return fail("count class");
    }
    if (pm_metal_dt_unit(a) < 0 || pm_metal_dt_unit(b) != pm_metal_dt_unit(a) + 1) {
        return fail("unit");
    }
    n = pm_metal_dt_count_class(PM_METAL_DT_CLASS_NET);
    found_a = 0;
    found_b = 0;
    for (i = 0; i < n; i++) {
        int32_t id = pm_metal_dt_by_class(PM_METAL_DT_CLASS_NET, i);
        if (id == a) {
            found_a = 1;
        }
        if (id == b) {
            found_b = 1;
        }
    }
    if (!found_a || !found_b) {
        return fail("by_class");
    }
    if (strcmp(pm_metal_dt_compat(a), "metal-dt-test") != 0) {
        return fail("compat");
    }
    {
        uint32_t loc0 = 0;
        if (pm_metal_dt_loc(a, 0, &loc0) != 0 || loc0 != 0) {
            return fail("loc0");
        }
        if (pm_metal_dt_loc(a, 1, &loc0) != 0 || loc0 != 1) {
            return fail("loc1");
        }
        if (pm_metal_dt_loc(a, 4, &loc0) != -1) {
            return fail("loc oob");
        }
    }
    if (pm_metal_dt_unbind(a) != 0) {
        return fail("unbind");
    }
    if (pm_metal_dt_count_class(PM_METAL_DT_CLASS_NET) != n0 + 1) {
        return fail("count after unbind");
    }
    if (pm_metal_dt_class(a) != -1) {
        return fail("gone");
    }
    if (pm_metal_dt_compat(b) == NULL) {
        return fail("survivor");
    }
    if (pm_metal_dt_unbind(a) != -1) {
        return fail("unbind gone");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.dt, tests, pm_metal_dt_tests);
