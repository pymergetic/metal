/* pymergetic.metal.drivers.input — bind two, inject, unbind one. */
#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/input.h"
#include "pymergetic/metal/drivers/input.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t s_last;
static uint32_t s_n;

static int32_t nop_open(void *ctx) {
    (void)ctx;
    return 0;
}

static int32_t rec_inject(void *ctx, int32_t key) {
    (void)ctx;
    if (key < 0) {
        return -1;
    }
    s_last = key;
    s_n++;
    return pm_metal_input_push(key);
}

static int32_t nop_poll(void *ctx) {
    (void)ctx;
    return 0;
}

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.input test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_input_tests(void) {
    pm_metal_input_ops_t oa;
    pm_metal_input_ops_t ob;
    int32_t da;
    int32_t db;
    int32_t ha;
    int32_t hb;
    int32_t n0;
    memset(&oa, 0, sizeof(oa));
    memset(&ob, 0, sizeof(ob));
    oa.open = nop_open;
    oa.poll = nop_poll;
    oa.inject = rec_inject;
    ob.open = nop_open;
    ob.poll = nop_poll;
    ob.inject = rec_inject;
    if (pm_metal_drivers_input_init(NULL) != -1) {
        return fail("init null");
    }
    n0 = pm_metal_drivers_input_count();
    da = pm_metal_dt_add(PM_METAL_DT_CLASS_INPUT, "test-in", PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, 0);
    db = pm_metal_dt_add(PM_METAL_DT_CLASS_INPUT, "test-in", PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, 1);
    ha = pm_metal_drivers_input_bind(da, &oa);
    hb = pm_metal_drivers_input_bind(db, &ob);
    if (ha < 0 || hb < 0 || ha == hb) {
        return fail("bind two");
    }
    if (pm_metal_drivers_input_count() != n0 + 2) {
        return fail("count");
    }
    s_n = 0;
    if (pm_metal_drivers_input_inject(ha, (int32_t)'Q') != 0) {
        return fail("inject");
    }
    if (s_n != 1u || s_last != (int32_t)'Q' || pm_metal_input_pop() != (int32_t)'Q') {
        return fail("push");
    }
    if (pm_metal_drivers_input_inject(ha, -1) == 0) {
        return fail("inject neg");
    }
    if (pm_metal_drivers_input_unbind(hb) != 0) {
        return fail("unbind b");
    }
    if (pm_metal_drivers_input_count() != n0 + 1) {
        return fail("count after");
    }
    if (pm_metal_drivers_input_unbind(ha) != 0) {
        return fail("unbind a");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers.input, tests, pm_metal_drivers_input_tests);
