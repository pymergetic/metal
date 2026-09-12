/* pymergetic.metal.drivers.blk.ide — ram disk, compatible ide-ata. */
#include "pymergetic/metal/drivers/blk/ide/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/blk.h"
#include "pymergetic/util/limits/__types__.h"
#include "pymergetic/util/mem.h"

#include <string.h>

#define IDE_SEC 512u
/* How many disks this card offers and how many sectors the largest of them
 * may hold: the disk itself was always taken at probe, the row it hangs off
 * is now too. */
#define IDE_DEVICE_DEFAULT 2u
#define IDE_SECTOR_DEFAULT 256u

struct ide_blk {
    uint32_t used;
    uint32_t nsec;
    uint32_t unit;
    uint8_t *data;
    int32_t dt_id;
    int32_t blk_h;
    pm_metal_blk_ops_t ops;
    struct ide_blk *next;
};

static pm_util_mem_arena_t *s_arena;
/* One row per disk, taken when it attaches and kept for the seat. The blk
 * core holds each row's address as its ops ctx, so rows are linked, never
 * moved. */
static struct ide_blk *s_head;
static uint32_t s_dev_used;

PM_UTIL_LIMIT_C(pm_metal_blk_ide_limit_device, pymergetic.metal.drivers.blk.ide, device,
    IDE_DEVICE_DEFAULT, 0u, &s_dev_used);
PM_UTIL_LIMIT_C(pm_metal_blk_ide_limit_sector, pymergetic.metal.drivers.blk.ide, sector,
    IDE_SECTOR_DEFAULT, 0u, NULL);

/* A row for one more disk: a closed one first, then a fresh one. Either way
 * the device knob says whether this card may offer another disk at all. */
static struct ide_blk *ide_row(void) {
    struct ide_blk *d = s_head;
    uint32_t rows = 0;
    if (!PM_UTIL_LIMIT_ROOM(pm_metal_blk_ide_limit_device, s_dev_used)) {
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
    d->blk_h = -1;
    d->next = s_head;
    s_head = d;
    return d;
}

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
    if (d->used != 0 && s_dev_used != 0) {
        s_dev_used--;
    }
    d->used = 0;
    d->data = NULL;
}

int32_t pm_metal_drivers_blk_ide_init(pm_util_mem_arena_t *arena) {
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

void pm_metal_drivers_blk_ide_deinit(void) {
    s_head = NULL;
    s_dev_used = 0;
    s_arena = NULL;
}

int32_t pm_metal_drivers_blk_ide_probe(uint32_t nsec) {
    struct ide_blk *d;
    size_t bytes;
    uint32_t most = pm_metal_blk_ide_limit_sector.soft;
    if (most == 0u) {
        most = pm_metal_blk_ide_limit_sector.dflt;
    }
    if (s_arena == NULL || nsec == 0 || nsec > most) {
        return -1;
    }
    d = ide_row();
    if (d == NULL) {
        return -1;
    }
    bytes = (size_t)nsec * IDE_SEC;
    if (d->data == NULL || d->nsec != nsec) {
        d->data = pm_util_mem_alloc(s_arena, bytes);
        if (d->data == NULL) {
            return -1;
        }
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
    d->dt_id = pm_metal_dt_add(PM_METAL_DT_CLASS_BLK, "ide-ata", PM_METAL_DT_BUS_ISA, 0x1f0u,
        d->unit, 0, 0);
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
    s_dev_used++;
    return d->blk_h;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk.ide, pm_metal_drivers_blk_ide_init, pm_metal_drivers_blk_ide_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk.ide, pm_metal_drivers_blk_ide_deinit, pm_metal_drivers_blk_ide_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk.ide, pm_metal_drivers_blk_ide_probe, pm_metal_drivers_blk_ide_probe, int32_t(uint32_t));

PM_MOD_BOOT_C(pymergetic.metal.drivers.blk.ide, pm_metal_drivers_blk_ide_init, pm_metal_drivers_blk_ide_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.blk.ide, pymergetic.metal.drivers.blk);
