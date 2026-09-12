/* pymergetic.metal.drivers.rtc.sim — in-process settable unix time. */
#include "pymergetic/metal/drivers/rtc/sim/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/rtc.h"
#include "pymergetic/util/limits/__types__.h"
#include "pymergetic/util/mem.h"

#include <string.h>

#define RTC_SIM_DEVICE_DEFAULT 4u

struct rtc_sim {
    uint32_t used;
    int64_t unix_s;
    uint32_t unit;
    int32_t dt_id;
    int32_t rtc_h;
    pm_metal_rtc_ops_t ops;
    struct rtc_sim *next;
};

static pm_util_mem_arena_t *s_arena;
/* One row per clock, taken when it attaches and kept for the seat. The rtc
 * core holds each row's address as its ops ctx, so rows are linked, never
 * moved. */
static struct rtc_sim *s_head;
static uint32_t s_dev_used;

PM_UTIL_LIMIT_C(pm_metal_rtc_sim_limit_device, pymergetic.metal.drivers.rtc.sim, device,
    RTC_SIM_DEVICE_DEFAULT, 0u, &s_dev_used);

/* A row for one more clock: a closed one first, then a fresh one. Either way
 * the device knob says whether this card may offer another clock at all. */
static struct rtc_sim *rtc_row(void) {
    struct rtc_sim *d = s_head;
    uint32_t rows = 0;
    if (!PM_UTIL_LIMIT_ROOM(pm_metal_rtc_sim_limit_device, s_dev_used)) {
        return NULL;
    }
    while (d != NULL) {
        if (!d->used) {
            return d;
        }
        rows++;
        d = d->next;
    }
    d = pm_util_mem_alloc(s_arena, sizeof(*d));
    if (d == NULL) {
        return NULL;
    }
    memset(d, 0, sizeof(*d));
    d->unit = rows;
    d->dt_id = -1;
    d->rtc_h = -1;
    d->next = s_head;
    s_head = d;
    return d;
}

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
        if (d->used != 0 && s_dev_used != 0) {
            s_dev_used--;
        }
        d->used = 0;
    }
}

int32_t pm_metal_drivers_rtc_sim_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    /* Rows came from the arena the last run was given; this one may be a
     * different arena, so the chain starts empty. */
    s_head = NULL;
    s_dev_used = 0;
    return 0;
}

void pm_metal_drivers_rtc_sim_deinit(void) {
    s_head = NULL;
    s_dev_used = 0;
    s_arena = NULL;
}

int32_t pm_metal_drivers_rtc_sim_probe(int64_t unix_s) {
    struct rtc_sim *d;
    if (s_arena == NULL) {
        return -1;
    }
    d = rtc_row();
    if (d == NULL) {
        return -1;
    }
    d->unix_s = unix_s;
    d->ops.get = sim_get;
    d->ops.set = sim_set;
    d->ops.close = sim_close;
    d->ops.ctx = d;
    d->used = 1;
    d->dt_id = pm_metal_dt_add(PM_METAL_DT_CLASS_RTC, "rtc-sim", PM_METAL_DT_BUS_PLATFORM, 0, 0,
        0, d->unit);
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
    s_dev_used++;
    return d->rtc_h;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.rtc.sim, pm_metal_drivers_rtc_sim_init, pm_metal_drivers_rtc_sim_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.rtc.sim, pm_metal_drivers_rtc_sim_deinit, pm_metal_drivers_rtc_sim_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.rtc.sim, pm_metal_drivers_rtc_sim_probe, pm_metal_drivers_rtc_sim_probe, int32_t(int64_t));

PM_MOD_BOOT_C(pymergetic.metal.drivers.rtc.sim, pm_metal_drivers_rtc_sim_init, pm_metal_drivers_rtc_sim_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.rtc.sim, pymergetic.metal.drivers.rtc);
