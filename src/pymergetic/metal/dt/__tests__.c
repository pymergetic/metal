/* pymergetic.metal.dt — two of the same compatible, unbind one. */
#include "pymergetic/metal/dt.h"

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
    if (pm_metal_dt_init(NULL) != -1) {
        return fail("init null");
    }
    a = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "virtio-net", PM_METAL_DT_BUS_PCI, 0, 1, 0, 0);
    b = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "virtio-net", PM_METAL_DT_BUS_PCI, 0, 2, 0, 0);
    if (a < 0 || b < 0 || a == b) {
        return fail("add two");
    }
    again = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "virtio-net", PM_METAL_DT_BUS_PCI, 0, 1, 0, 0);
    if (again != a) {
        return fail("idempotent");
    }
    if (pm_metal_dt_count_class(PM_METAL_DT_CLASS_NET) != 2) {
        return fail("count class");
    }
    if (pm_metal_dt_unit(a) != 0 || pm_metal_dt_unit(b) != 1) {
        return fail("unit");
    }
    if (pm_metal_dt_by_class(PM_METAL_DT_CLASS_NET, 0) != a
        || pm_metal_dt_by_class(PM_METAL_DT_CLASS_NET, 1) != b) {
        return fail("by_class");
    }
    if (strcmp(pm_metal_dt_compat(a), "virtio-net") != 0) {
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
    if (pm_metal_dt_count_class(PM_METAL_DT_CLASS_NET) != 1) {
        return fail("count after unbind");
    }
    if (pm_metal_dt_by_class(PM_METAL_DT_CLASS_NET, 0) != b) {
        return fail("survivor");
    }
    if (pm_metal_dt_unbind(a) != -1) {
        return fail("unbind gone");
    }
    return 0;
}
