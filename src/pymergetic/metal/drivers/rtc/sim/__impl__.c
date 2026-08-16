/* pymergetic.metal.drivers.rtc.sim — in-process settable unix time. */
#include "pymergetic/metal/drivers/rtc/sim/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/rtc.h"

#include <string.h>

#define RTC_SIM_MAX 4u

struct rtc_sim {
    uint32_t used;
    int64_t unix_s;
    int32_t dt_id;
    int32_t rtc_h;
    pm_metal_rtc_ops_t ops;
};

static pm_util_mem_arena_t *s_arena;
static struct rtc_sim s_dev[RTC_SIM_MAX];

static int64_t sim_get(void *ctx) {
    struct rtc_sim *d = ctx;
    return d != NULL ? d->unix_s : -1;
}

static int32_t sim_set(void *ctx, int64_t unix_s) {
    struct rtc_sim *d = ctx;
    if (d == NULL || unix_s < 0) {
        return -1;
    }
    d->unix_s = unix_s;
    return 0;
}

static void sim_close(void *ctx) {
    struct rtc_sim *d = ctx;
    if (d != NULL) {
        d->used = 0;
    }
}

int32_t pm_metal_drivers_rtc_sim_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_dev, 0, sizeof(s_dev));
    return 0;
}

void pm_metal_drivers_rtc_sim_deinit(void) {
    memset(s_dev, 0, sizeof(s_dev));
    s_arena = NULL;
}

int32_t pm_metal_drivers_rtc_sim_probe(int64_t unix_s) {
    uint32_t i;
    struct rtc_sim *d;
    if (s_arena == NULL) {
        return -1;
    }
    for (i = 0; i < RTC_SIM_MAX; i++) {
        if (s_dev[i].used) {
            continue;
        }
        d = &s_dev[i];
        memset(d, 0, sizeof(*d));
        d->used = 1;
        d->unix_s = unix_s;
        d->ops.get = sim_get;
        d->ops.set = sim_set;
        d->ops.close = sim_close;
        d->ops.ctx = d;
        d->dt_id = pm_metal_dt_add(PM_METAL_DT_CLASS_RTC, "rtc-sim", PM_METAL_DT_BUS_PLATFORM, 0, 0,
            0, i);
        if (d->dt_id < 0) {
            d->used = 0;
            return -1;
        }
        d->rtc_h = pm_metal_drivers_rtc_bind(d->dt_id, &d->ops);
        if (d->rtc_h < 0) {
            (void)pm_metal_dt_unbind(d->dt_id);
            d->used = 0;
            return -1;
        }
        return d->rtc_h;
    }
    return -1;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.rtc.sim, pm_metal_drivers_rtc_sim_init, pm_metal_drivers_rtc_sim_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.rtc.sim, pm_metal_drivers_rtc_sim_deinit, pm_metal_drivers_rtc_sim_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.rtc.sim, pm_metal_drivers_rtc_sim_probe, pm_metal_drivers_rtc_sim_probe, int32_t(int64_t));

PM_MOD_BOOT_C(pymergetic.metal.drivers.rtc.sim, pm_metal_drivers_rtc_sim_init, pm_metal_drivers_rtc_sim_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.rtc.sim, pymergetic.metal.drivers.rtc);
