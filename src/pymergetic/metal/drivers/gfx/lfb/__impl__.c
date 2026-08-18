/* pymergetic.metal.drivers.gfx.lfb — instanced scanout. Present copies RGB888 into a shadow. */
#include "pymergetic/metal/drivers/gfx/lfb/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/gfx.h"

#include <string.h>

#define GFX_FILL_MAX 4u
#define GFX_SHADOW_W 32u
#define GFX_SHADOW_H 16u
#define GFX_SHADOW_STRIDE (GFX_SHADOW_W * 3u)

struct gfx_fill {
    uint32_t used;
    uint32_t w;
    uint32_t h;
    uint32_t stride;
    uint8_t *fb;
    uint32_t fb_w;
    uint32_t fb_h;
    uint32_t fb_stride;
    uint8_t shadow[GFX_SHADOW_H * GFX_SHADOW_STRIDE];
    int32_t dt_id;
    int32_t gfx_h;
    pm_metal_gfx_ops_t ops;
};

static pm_util_mem_arena_t *s_arena;
static struct gfx_fill s_dev[GFX_FILL_MAX];

static int32_t fill_open(void *ctx) {
    return ctx != NULL ? 0 : -1;
}

static void fill_close(void *ctx) {
    struct gfx_fill *d = ctx;
    if (d != NULL) {
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
    if (d->fb != NULL) {
        cw = w < d->fb_w ? w : d->fb_w;
        ch = h < d->fb_h ? h : d->fb_h;
        for (y = 0; y < ch; y++) {
            memcpy(d->fb + y * d->fb_stride, pix + y * stride, cw * 3u);
        }
    } else {
        cw = w < GFX_SHADOW_W ? w : GFX_SHADOW_W;
        ch = h < GFX_SHADOW_H ? h : GFX_SHADOW_H;
        for (y = 0; y < ch; y++) {
            memcpy(d->shadow + y * GFX_SHADOW_STRIDE, pix + y * stride, cw * 3u);
        }
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
    if (d->fb != NULL) {
        if (w != NULL) {
            *w = d->fb_w;
        }
        if (h != NULL) {
            *h = d->fb_h;
        }
        if (stride != NULL) {
            *stride = d->fb_stride;
        }
        return 0;
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

static int32_t lfb_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2, uint32_t loc3) {
    uint32_t i;
    struct gfx_fill *d;
    int32_t dt;
    if (s_arena == NULL) {
        return -1;
    }
    dt = pm_metal_dt_add(PM_METAL_DT_CLASS_GFX, "lfb", bus, loc0, loc1, loc2, loc3);
    if (dt < 0) {
        return -1;
    }
    for (i = 0; i < GFX_FILL_MAX; i++) {
        if (s_dev[i].used && s_dev[i].dt_id == dt) {
            return s_dev[i].gfx_h;
        }
    }
    for (i = 0; i < GFX_FILL_MAX; i++) {
        if (s_dev[i].used) {
            continue;
        }
        d = &s_dev[i];
        memset(d, 0, sizeof(*d));
        d->used = 1;
        d->ops.open = fill_open;
        d->ops.close = fill_close;
        d->ops.present = fill_present;
        d->ops.poll = fill_poll;
        d->ops.info = fill_info;
        d->ops.ctx = d;
        d->dt_id = dt;
        d->gfx_h = pm_metal_drivers_gfx_bind(dt, &d->ops);
        if (d->gfx_h < 0) {
            d->used = 0;
            return -1;
        }
        return d->gfx_h;
    }
    return -1;
}

int32_t pm_metal_drivers_gfx_lfb_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_dev, 0, sizeof(s_dev));
    return 0;
}

void pm_metal_drivers_gfx_lfb_deinit(void) {
    memset(s_dev, 0, sizeof(s_dev));
    s_arena = NULL;
}

int32_t pm_metal_drivers_gfx_lfb_probe(void) {
    uint32_t i;
    for (i = 0; i < GFX_FILL_MAX; i++) {
        if (!s_dev[i].used) {
            return lfb_attach(PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, i);
        }
    }
    return -1;
}

int32_t pm_metal_drivers_gfx_lfb_bind(uint8_t *pix, uint32_t w, uint32_t h, uint32_t stride) {
    int32_t gh;
    uint32_t i;
    if (pix == NULL || w == 0 || h == 0 || stride < w * 3u) {
        return -1;
    }
    gh = pm_metal_drivers_gfx_lfb_probe();
    if (gh < 0) {
        return -1;
    }
    for (i = 0; i < GFX_FILL_MAX; i++) {
        if (s_dev[i].used && s_dev[i].gfx_h == gh) {
            s_dev[i].fb = pix;
            s_dev[i].fb_w = w;
            s_dev[i].fb_h = h;
            s_dev[i].fb_stride = stride;
            s_dev[i].w = w;
            s_dev[i].h = h;
            s_dev[i].stride = stride;
            return gh;
        }
    }
    return -1;
}

int32_t pm_metal_drivers_gfx_lfb_up(void) {
    uint32_t i;
    if (s_arena == NULL) {
        return -1;
    }
    for (i = 0; i < GFX_FILL_MAX; i++) {
        if (s_dev[i].used) {
            return 0;
        }
    }
    return pm_metal_drivers_gfx_lfb_probe() >= 0 ? 0 : -1;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.gfx.lfb, pm_metal_drivers_gfx_lfb_init, pm_metal_drivers_gfx_lfb_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.gfx.lfb, pm_metal_drivers_gfx_lfb_deinit, pm_metal_drivers_gfx_lfb_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.gfx.lfb, pm_metal_drivers_gfx_lfb_probe, pm_metal_drivers_gfx_lfb_probe, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.gfx.lfb, pm_metal_drivers_gfx_lfb_up, pm_metal_drivers_gfx_lfb_up, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.gfx.lfb, pm_metal_drivers_gfx_lfb_bind, pm_metal_drivers_gfx_lfb_bind, int32_t(uint8_t *, uint32_t, uint32_t, uint32_t));

PM_MOD_BOOT_C(pymergetic.metal.drivers.gfx.lfb, pm_metal_drivers_gfx_lfb_init, pm_metal_drivers_gfx_lfb_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.gfx.lfb, pymergetic.metal.drivers.gfx);

