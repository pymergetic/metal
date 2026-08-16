/* pymergetic.metal.drivers.blk.ide — ram disk, compatible ide-ata. */
#include "pymergetic/metal/drivers/blk/ide/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/blk.h"
#include "pymergetic/util/mem.h"

#include <string.h>

#define IDE_MAX 2u
#define IDE_SEC 512u

struct ide_blk {
    uint32_t used;
    uint32_t nsec;
    uint8_t *data;
    int32_t dt_id;
    int32_t blk_h;
    pm_metal_blk_ops_t ops;
};

static pm_util_mem_arena_t *s_arena;
static struct ide_blk s_dev[IDE_MAX];

static int32_t ide_ready(void *ctx) {
    struct ide_blk *d = ctx;
    return (d != NULL && d->data != NULL) ? 1 : 0;
}

static uint64_t ide_cap(void *ctx) {
    struct ide_blk *d = ctx;
    return d != NULL ? d->nsec : 0;
}

static int32_t ide_read(void *ctx, uint64_t lba, void *buf, uint32_t nsec) {
    struct ide_blk *d = ctx;
    if (d == NULL || d->data == NULL || buf == NULL || lba + nsec > d->nsec) {
        return -1;
    }
    memcpy(buf, d->data + lba * IDE_SEC, nsec * IDE_SEC);
    return 0;
}

static int32_t ide_write(void *ctx, uint64_t lba, const void *buf, uint32_t nsec) {
    struct ide_blk *d = ctx;
    if (d == NULL || d->data == NULL || buf == NULL || lba + nsec > d->nsec) {
        return -1;
    }
    memcpy(d->data + lba * IDE_SEC, buf, nsec * IDE_SEC);
    return 0;
}

static void ide_close(void *ctx) {
    struct ide_blk *d = ctx;
    if (d == NULL) {
        return;
    }
    d->used = 0;
    d->data = NULL;
}

int32_t pm_metal_drivers_blk_ide_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_dev, 0, sizeof(s_dev));
    return 0;
}

void pm_metal_drivers_blk_ide_deinit(void) {
    memset(s_dev, 0, sizeof(s_dev));
    s_arena = NULL;
}

int32_t pm_metal_drivers_blk_ide_probe(uint32_t nsec) {
    uint32_t i;
    struct ide_blk *d;
    size_t bytes;
    if (s_arena == NULL || nsec == 0 || nsec > 256u) {
        return -1;
    }
    for (i = 0; i < IDE_MAX; i++) {
        if (s_dev[i].used) {
            continue;
        }
        d = &s_dev[i];
        memset(d, 0, sizeof(*d));
        bytes = (size_t)nsec * IDE_SEC;
        d->data = pm_util_mem_alloc(s_arena, bytes);
        if (d->data == NULL) {
            return -1;
        }
        memset(d->data, 0, bytes);
        d->nsec = nsec;
        d->used = 1;
        d->ops.ready = ide_ready;
        d->ops.capacity = ide_cap;
        d->ops.read = ide_read;
        d->ops.write = ide_write;
        d->ops.close = ide_close;
        d->ops.ctx = d;
        d->dt_id = pm_metal_dt_add(PM_METAL_DT_CLASS_BLK, "ide-ata", PM_METAL_DT_BUS_ISA, 0x1f0u, i,
            0, 0);
        if (d->dt_id < 0) {
            d->used = 0;
            return -1;
        }
        d->blk_h = pm_metal_drivers_blk_bind(d->dt_id, &d->ops);
        if (d->blk_h < 0) {
            (void)pm_metal_dt_unbind(d->dt_id);
            d->used = 0;
            return -1;
        }
        return d->blk_h;
    }
    return -1;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk.ide, pm_metal_drivers_blk_ide_init, pm_metal_drivers_blk_ide_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk.ide, pm_metal_drivers_blk_ide_deinit, pm_metal_drivers_blk_ide_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk.ide, pm_metal_drivers_blk_ide_probe, pm_metal_drivers_blk_ide_probe, int32_t(uint32_t));

PM_MOD_BOOT_C(pymergetic.metal.drivers.blk.ide, pm_metal_drivers_blk_ide_init, pm_metal_drivers_blk_ide_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.blk.ide, pymergetic.metal.drivers.blk);
