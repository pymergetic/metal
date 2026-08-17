/* pymergetic.metal.drivers.net — bind two, unbind one. */
#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_mac_a[6] = { 1, 0, 0, 0, 0, 1 };
static uint8_t s_mac_b[6] = { 1, 0, 0, 0, 0, 2 };

static int32_t nop_open(void *ctx) {
    (void)ctx;
    return 0;
}

static void mac_a(void *ctx, uint8_t out[6]) {
    (void)ctx;
    memcpy(out, s_mac_a, 6);
}

static void mac_b(void *ctx, uint8_t out[6]) {
    (void)ctx;
    memcpy(out, s_mac_b, 6);
}

static int32_t nop_tx(void *ctx, const uint8_t *frame, uint16_t len) {
    (void)ctx;
    (void)frame;
    (void)len;
    return 0;
}

static int32_t nop_poll(void *ctx) {
    (void)ctx;
    return 0;
}

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.drivers.net test: %s\n", why);
    return 1;
}

int32_t pm_metal_drivers_net_tests(void) {
    pm_metal_netdev_ops_t oa;
    pm_metal_netdev_ops_t ob;
    int32_t da;
    int32_t db;
    int32_t ha;
    int32_t hb;
    int32_t n0;
    uint8_t mac[6];
    memset(&oa, 0, sizeof(oa));
    memset(&ob, 0, sizeof(ob));
    oa.open = nop_open;
    oa.mac = mac_a;
    oa.tx = nop_tx;
    oa.poll = nop_poll;
    ob.open = nop_open;
    ob.mac = mac_b;
    ob.tx = nop_tx;
    ob.poll = nop_poll;
    if (pm_metal_drivers_net_init(NULL) != -1) {
        return fail("init null");
    }
    n0 = pm_metal_drivers_net_count();
    da = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "test-nic", PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, 0);
    db = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "test-nic", PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, 1);
    ha = pm_metal_drivers_net_bind(da, &oa);
    hb = pm_metal_drivers_net_bind(db, &ob);
    if (ha < 0 || hb < 0 || ha == hb) {
        return fail("bind two");
    }
    if (pm_metal_drivers_net_count() != n0 + 2) {
        return fail("count");
    }
    pm_metal_drivers_net_mac(ha, mac);
    if (mac[5] != 1) {
        return fail("mac a");
    }
    if (pm_metal_drivers_net_unbind(hb) != 0) {
        return fail("unbind b");
    }
    if (pm_metal_drivers_net_count() != n0 + 1) {
        return fail("count after");
    }
    if (pm_metal_drivers_net_unbind(ha) != 0) {
        return fail("unbind a");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.drivers.net, tests, pm_metal_drivers_net_tests);
