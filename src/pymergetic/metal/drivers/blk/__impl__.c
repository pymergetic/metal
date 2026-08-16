/* pymergetic.metal.drivers.blk — block device table. */
#include "pymergetic/metal/drivers/blk/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/__types__.h"

#include <string.h>

#define PM_METAL_BLK_MAX 8u

struct pm_metal_blkdev {
    uint32_t used;
    int32_t dt_id;
    pm_metal_blk_ops_t ops;
};

static pm_util_mem_arena_t *s_arena;
static struct pm_metal_blkdev s_dev[PM_METAL_BLK_MAX];

int32_t pm_metal_drivers_blk_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_dev, 0, sizeof(s_dev));
    return 0;
}

void pm_metal_drivers_blk_deinit(void) {
    uint32_t i;
    for (i = 0; i < PM_METAL_BLK_MAX; i++) {
        if (s_dev[i].used && s_dev[i].ops.close != NULL) {
            s_dev[i].ops.close(s_dev[i].ops.ctx);
        }
    }
    memset(s_dev, 0, sizeof(s_dev));
    s_arena = NULL;
}

int32_t pm_metal_drivers_blk_bind(int32_t dt_id, const pm_metal_blk_ops_t *ops) {
    uint32_t i;
    if (s_arena == NULL || dt_id < 0 || ops == NULL) {
        return -1;
    }
    for (i = 0; i < PM_METAL_BLK_MAX; i++) {
        if (s_dev[i].used && s_dev[i].dt_id == dt_id) {
            return (int32_t)i;
        }
    }
    for (i = 0; i < PM_METAL_BLK_MAX; i++) {
        if (!s_dev[i].used) {
            s_dev[i].used = 1;
            s_dev[i].dt_id = dt_id;
            s_dev[i].ops = *ops;
            return (int32_t)i;
        }
    }
    return -1;
}

int32_t pm_metal_drivers_blk_unbind(int32_t h) {
    if (h < 0 || (uint32_t)h >= PM_METAL_BLK_MAX || !s_dev[h].used) {
        return -1;
    }
    if (s_dev[h].ops.close != NULL) {
        s_dev[h].ops.close(s_dev[h].ops.ctx);
    }
    memset(&s_dev[h], 0, sizeof(s_dev[h]));
    return 0;
}

int32_t pm_metal_drivers_blk_unbind_dt(int32_t dt_id) {
    uint32_t i;
    int32_t st = -1;
    for (i = 0; i < PM_METAL_BLK_MAX; i++) {
        if (s_dev[i].used && s_dev[i].dt_id == dt_id) {
            st = pm_metal_drivers_blk_unbind((int32_t)i);
        }
    }
    return st;
}

int32_t pm_metal_drivers_blk_count(void) {
    uint32_t i;
    int32_t n = 0;
    for (i = 0; i < PM_METAL_BLK_MAX; i++) {
        if (s_dev[i].used) {
            n++;
        }
    }
    return n;
}

int32_t pm_metal_drivers_blk_dt_id(int32_t h) {
    if (h < 0 || (uint32_t)h >= PM_METAL_BLK_MAX || !s_dev[h].used) {
        return -1;
    }
    return s_dev[h].dt_id;
}

int32_t pm_metal_drivers_blk_by_dt(int32_t dt_id) {
    uint32_t i;
    if (dt_id < 0) {
        return -1;
    }
    for (i = 0; i < PM_METAL_BLK_MAX; i++) {
        if (s_dev[i].used && s_dev[i].dt_id == dt_id) {
            return (int32_t)i;
        }
    }
    return -1;
}

int32_t pm_metal_drivers_blk_by_compat(const char *compat, int32_t nth) {
    uint32_t i;
    int32_t seen = 0;
    const char *c;
    if (compat == NULL || compat[0] == 0 || nth < 0) {
        return -1;
    }
    for (i = 0; i < PM_METAL_BLK_MAX; i++) {
        if (!s_dev[i].used) {
            continue;
        }
        c = pm_metal_dt_compat(s_dev[i].dt_id);
        if (c == NULL || strcmp(c, compat) != 0) {
            continue;
        }
        if (seen == nth) {
            return (int32_t)i;
        }
        seen++;
    }
    return -1;
}

int32_t pm_metal_drivers_blk_ready(int32_t h) {
    if (h < 0 || (uint32_t)h >= PM_METAL_BLK_MAX || !s_dev[h].used) {
        return 0;
    }
    if (s_dev[h].ops.ready == NULL) {
        return 1;
    }
    return s_dev[h].ops.ready(s_dev[h].ops.ctx);
}

uint64_t pm_metal_drivers_blk_capacity(int32_t h) {
    if (h < 0 || (uint32_t)h >= PM_METAL_BLK_MAX || !s_dev[h].used || s_dev[h].ops.capacity == NULL) {
        return 0;
    }
    return s_dev[h].ops.capacity(s_dev[h].ops.ctx);
}

int32_t pm_metal_drivers_blk_read(int32_t h, uint64_t lba, void *buf, uint32_t nsec) {
    if (h < 0 || (uint32_t)h >= PM_METAL_BLK_MAX || !s_dev[h].used || s_dev[h].ops.read == NULL) {
        return -1;
    }
    return s_dev[h].ops.read(s_dev[h].ops.ctx, lba, buf, nsec);
}

int32_t pm_metal_drivers_blk_write(int32_t h, uint64_t lba, const void *buf, uint32_t nsec) {
    if (h < 0 || (uint32_t)h >= PM_METAL_BLK_MAX || !s_dev[h].used || s_dev[h].ops.write == NULL) {
        return -1;
    }
    return s_dev[h].ops.write(s_dev[h].ops.ctx, lba, buf, nsec);
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk, pm_metal_drivers_blk_init, pm_metal_drivers_blk_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk, pm_metal_drivers_blk_deinit, pm_metal_drivers_blk_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk, pm_metal_drivers_blk_bind, pm_metal_drivers_blk_bind, int32_t(int32_t, const pm_metal_blk_ops_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk, pm_metal_drivers_blk_unbind, pm_metal_drivers_blk_unbind, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk, pm_metal_drivers_blk_unbind_dt, pm_metal_drivers_blk_unbind_dt, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk, pm_metal_drivers_blk_count, pm_metal_drivers_blk_count, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk, pm_metal_drivers_blk_dt_id, pm_metal_drivers_blk_dt_id, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk, pm_metal_drivers_blk_by_dt, pm_metal_drivers_blk_by_dt, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk, pm_metal_drivers_blk_by_compat, pm_metal_drivers_blk_by_compat, int32_t(const char *, int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk, pm_metal_drivers_blk_ready, pm_metal_drivers_blk_ready, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk, pm_metal_drivers_blk_capacity, pm_metal_drivers_blk_capacity, uint64_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk, pm_metal_drivers_blk_read, pm_metal_drivers_blk_read, int32_t(int32_t, uint64_t, void *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk, pm_metal_drivers_blk_write, pm_metal_drivers_blk_write, int32_t(int32_t, uint64_t, const void *, uint32_t));

PM_MOD_BOOT_C(pymergetic.metal.drivers.blk, pm_metal_drivers_blk_init, pm_metal_drivers_blk_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.blk, pymergetic.metal.drivers);

static int32_t blk_class_unbind(int32_t dt_id) {
    return pm_metal_drivers_blk_unbind_dt(dt_id);
}

PM_METAL_CLASS_C(PM_METAL_DT_CLASS_BLK, blk_class_unbind);
