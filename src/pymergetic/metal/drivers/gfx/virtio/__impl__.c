/* pymergetic.metal.drivers.gfx.virtio — instanced scanout. Present copies RGB888 into a shadow. */
#include "pymergetic/metal/drivers/gfx/virtio/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/gfx.h"
#include "pymergetic/util/limits/__types__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/metal/drivers/__types__.h"
#include "pymergetic/metal/bus/virtio.h"

#include <string.h>

#define GFX_DEVICE_DEFAULT 4u
#define GFX_SHADOW_W 32u
#define GFX_SHADOW_H 16u
#define GFX_SHADOW_STRIDE (GFX_SHADOW_W * 3u)
#define GFX_SHADOW_DEFAULT (GFX_SHADOW_H * GFX_SHADOW_STRIDE)

struct gfx_fill {
    uint32_t used;
    uint32_t w;
    uint32_t h;
    uint32_t stride;
    uint8_t *shadow;
    uint32_t shadow_n;
    uint32_t unit;
    int32_t dt_id;
    int32_t gfx_h;
    pm_metal_gfx_ops_t ops;
    struct gfx_fill *next;
};

static pm_util_mem_arena_t *s_arena;
/* One row per scanout, taken when it attaches and kept for the seat. The
 * gfx core holds each row's address as its ops ctx, so rows are linked,
 * never moved. What a scanout costs is the shadow it keeps of a presented
 * frame, and that is taken at attach too: the knobs are how many scanouts
 * this card offers and how many bytes of a frame each one keeps. */
static struct gfx_fill *s_head;
static uint32_t s_dev_used;

PM_UTIL_LIMIT_C(pm_metal_gfx_virtio_limit_device, pymergetic.metal.drivers.gfx.virtio, device,
    GFX_DEVICE_DEFAULT, 0u, &s_dev_used);
PM_UTIL_LIMIT_C(pm_metal_gfx_virtio_limit_shadow, pymergetic.metal.drivers.gfx.virtio, shadow,
    GFX_SHADOW_DEFAULT, 0u, NULL);

/* This scanout's shadow, at the size the knob says now. Kept across a
 * close/attach cycle when it already matches. */
static int32_t shadow_fit(struct gfx_fill *d) {
    uint32_t want = pm_metal_gfx_virtio_limit_shadow.soft;
    uint8_t *shadow;
    if (want == 0u) {
        want = pm_metal_gfx_virtio_limit_shadow.dflt;
    }
    if (d->shadow != NULL && d->shadow_n == want) {
        return 0;
    }
    shadow = pm_util_mem_alloc(s_arena, want);
    if (shadow == NULL) {
        return -1;
    }
    memset(shadow, 0, want);
    d->shadow = shadow;
    d->shadow_n = want;
    return 0;
}

/* A row for one more scanout: a closed one first, then a fresh one. Either
 * way the device knob says whether this card may offer another scanout at
 * all — a closed row is one this card already paid for, not a free pass over
 * the knob. */
static struct gfx_fill *gfx_row(void) {
    struct gfx_fill *d = s_head;
    uint32_t rows = 0;
    if (!PM_UTIL_LIMIT_ROOM(pm_metal_gfx_virtio_limit_device, s_dev_used)) {
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
    d->gfx_h = -1;
    d->next = s_head;
    s_head = d;
    return d;
}

static int32_t fill_open(void *ctx) {
    return ctx != NULL ? 0 : -1;
}

static void fill_close(void *ctx) {
    struct gfx_fill *d = ctx;
    if (d != NULL) {
        if (d->used != 0 && s_dev_used != 0) {
            s_dev_used--;
        }
        d->used = 0;
        d->dt_id = -1;
        d->gfx_h = -1;
    }
}

static int32_t fill_present(void *ctx, const uint8_t *pix, uint32_t w, uint32_t h, uint32_t stride) {
    struct gfx_fill *d = ctx;
    uint32_t y;
    uint32_t cw;
    uint32_t ch;
    if (d == NULL || pix == NULL || w == 0 || h == 0 || stride < w * 3u) {
        return -1;
    }
    cw = w < GFX_SHADOW_W ? w : GFX_SHADOW_W;
    ch = d->shadow_n / GFX_SHADOW_STRIDE;
    if (h < ch) {
        ch = h;
    }
    for (y = 0; y < ch; y++) {
        memcpy(d->shadow + y * GFX_SHADOW_STRIDE, pix + y * stride, cw * 3u);
    }
    d->w = w;
    d->h = h;
    d->stride = stride;
    return 0;
}

static int32_t fill_poll(void *ctx) {
    return ctx != NULL ? 0 : -1;
}

static int32_t fill_info(void *ctx, uint32_t *w, uint32_t *h, uint32_t *stride) {
    struct gfx_fill *d = ctx;
    if (d == NULL) {
        return -1;
    }
    if (w != NULL) {
        *w = d->w;
    }
    if (h != NULL) {
        *h = d->h;
    }
    if (stride != NULL) {
        *stride = d->stride;
    }
    return 0;
}

static int32_t virtio_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2, uint32_t loc3) {
    struct gfx_fill *d;
    int32_t dt;
    if (s_arena == NULL) {
        return -1;
    }
    dt = pm_metal_dt_add(PM_METAL_DT_CLASS_GFX, "virtio-gpu", bus, loc0, loc1, loc2, loc3);
    if (dt < 0) {
        return -1;
    }
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used && d->dt_id == dt) {
            return d->gfx_h;
        }
    }
    d = gfx_row();
    if (d == NULL) {
        return -1;
    }
    if (shadow_fit(d) != 0) {
        return -1;
    }
    d->ops.open = fill_open;
    d->ops.close = fill_close;
    d->ops.present = fill_present;
    d->ops.poll = fill_poll;
    d->ops.info = fill_info;
    d->ops.ctx = d;
    d->dt_id = dt;
    d->used = 1;
    d->gfx_h = pm_metal_drivers_gfx_bind(dt, &d->ops);
    if (d->gfx_h < 0) {
        d->used = 0;
        return -1;
    }
    s_dev_used++;
    return d->gfx_h;
}

int32_t pm_metal_drivers_gfx_virtio_init(pm_util_mem_arena_t *arena) {
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

void pm_metal_drivers_gfx_virtio_deinit(void) {
    s_head = NULL;
    s_dev_used = 0;
    s_arena = NULL;
}

int32_t pm_metal_drivers_gfx_virtio_probe(void) {
    struct gfx_fill *d;
    uint32_t unit = 0;
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used) {
            unit++;
        }
    }
    return virtio_attach(PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, unit);
}

int32_t pm_metal_drivers_gfx_virtio_up(void) {
    struct gfx_fill *d;
    if (s_arena == NULL) {
        return -1;
    }
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used) {
            return 0;
        }
    }
    return pm_metal_drivers_gfx_virtio_probe() >= 0 ? 0 : -1;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.gfx.virtio, pm_metal_drivers_gfx_virtio_init, pm_metal_drivers_gfx_virtio_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.gfx.virtio, pm_metal_drivers_gfx_virtio_deinit, pm_metal_drivers_gfx_virtio_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.gfx.virtio, pm_metal_drivers_gfx_virtio_probe, pm_metal_drivers_gfx_virtio_probe, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.gfx.virtio, pm_metal_drivers_gfx_virtio_up, pm_metal_drivers_gfx_virtio_up, int32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.drivers.gfx.virtio, pm_metal_drivers_gfx_virtio_init, pm_metal_drivers_gfx_virtio_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.gfx.virtio, pymergetic.metal.drivers.gfx);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.gfx.virtio, pymergetic.metal.bus.virtio);

static int32_t virtio_drv_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2,
    uint32_t loc3) {
    return virtio_attach(bus, loc0, loc1, loc2, loc3) >= 0 ? 0 : -1;
}

PM_METAL_DRV_PCI_C(pymergetic.metal.drivers.gfx.virtio, PM_METAL_BUS_VIRTIO_VENDOR, PM_METAL_BUS_VIRTIO_DEV_GPU, virtio_drv_attach);
PM_METAL_DRV_PCI_C(pymergetic.metal.drivers.gfx.virtio, PM_METAL_BUS_VIRTIO_VENDOR, PM_METAL_BUS_VIRTIO_DEV_GPU_LEGACY, virtio_drv_attach);

