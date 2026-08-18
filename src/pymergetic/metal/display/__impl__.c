/* pymergetic.metal.display — RGB888 pixels; optional gfx handle for present. */
#include "pymergetic/metal/display/__exports__.h"

#include "pymergetic/metal/drivers/gfx.h"
#include "pymergetic/util/mem.h"

#include <string.h>

#ifndef PM_METAL_DISPLAY_DEF_W
#define PM_METAL_DISPLAY_DEF_W 32u
#endif
#ifndef PM_METAL_DISPLAY_DEF_H
#define PM_METAL_DISPLAY_DEF_H 16u
#endif

static pm_util_mem_arena_t *s_arena;
static uint8_t *s_pix;
static uint32_t s_w;
static uint32_t s_h;
static uint32_t s_stride;
static int32_t s_gfx_h;

int32_t pm_metal_display_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    s_pix = NULL;
    s_w = 0;
    s_h = 0;
    s_stride = 0;
    s_gfx_h = -1;
    return 0;
}

void pm_metal_display_deinit(void) {
    s_pix = NULL;
    s_w = 0;
    s_h = 0;
    s_stride = 0;
    s_gfx_h = -1;
    s_arena = NULL;
}

int32_t pm_metal_display_attach(uint8_t *pix, uint32_t w, uint32_t h, uint32_t stride) {
    if (s_arena == NULL || pix == NULL || w == 0 || h == 0 || stride < w * 3u) {
        return -1;
    }
    s_pix = pix;
    s_w = w;
    s_h = h;
    s_stride = stride;
    s_gfx_h = -1;
    return 0;
}

int32_t pm_metal_display_attach_h(int32_t gfx_h) {
    uint32_t w = 0;
    uint32_t h = 0;
    uint32_t stride = 0;
    if (s_arena == NULL || gfx_h < 0) {
        return -1;
    }
    if (pm_metal_drivers_gfx_info(gfx_h, &w, &h, &stride) != 0) {
        return -1;
    }
    if (s_pix == NULL) {
        if (w == 0 || h == 0) {
            w = PM_METAL_DISPLAY_DEF_W;
            h = PM_METAL_DISPLAY_DEF_H;
            stride = w * 3u;
        }
        if (stride < w * 3u) {
            stride = w * 3u;
        }
        s_pix = pm_util_mem_alloc(s_arena, (size_t)stride * (size_t)h);
        if (s_pix == NULL) {
            return -1;
        }
        memset(s_pix, 0, (size_t)stride * (size_t)h);
        s_w = w;
        s_h = h;
        s_stride = stride;
    }
    s_gfx_h = gfx_h;
    return 0;
}

int32_t pm_metal_display_up(void) {
    int32_t h = pm_metal_drivers_gfx_by_compat("virtio-gpu", 0);
    if (h < 0) {
        h = pm_metal_drivers_gfx_by_compat("sim", 0);
    }
    if (h < 0) {
        return -1;
    }
    if (pm_metal_display_attach_h(h) != 0) {
        return -1;
    }
    if (pm_metal_display_put(0, 0, 0x00ff0000u) != 0) {
        return -1;
    }
    return pm_metal_display_present();
}

int32_t pm_metal_display_present(void) {
    if (s_pix == NULL || s_gfx_h < 0) {
        return -1;
    }
    return pm_metal_drivers_gfx_present(s_gfx_h, s_pix, s_w, s_h, s_stride);
}

int32_t pm_metal_display_gfx_h(void) {
    return s_gfx_h;
}

int32_t pm_metal_display_put(uint32_t x, uint32_t y, uint32_t rgb) {
    uint8_t *p;
    if (s_pix == NULL || x >= s_w || y >= s_h) {
        return -1;
    }
    p = s_pix + y * s_stride + x * 3u;
    p[0] = (uint8_t)((rgb >> 16) & 0xffu);
    p[1] = (uint8_t)((rgb >> 8) & 0xffu);
    p[2] = (uint8_t)(rgb & 0xffu);
    return 0;
}

uint32_t pm_metal_display_get(uint32_t x, uint32_t y) {
    const uint8_t *p;
    if (s_pix == NULL || x >= s_w || y >= s_h) {
        return 0;
    }
    p = s_pix + y * s_stride + x * 3u;
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

uint32_t pm_metal_display_width(void) {
    return s_w;
}

uint32_t pm_metal_display_height(void) {
    return s_h;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.display, pm_metal_display_init, pm_metal_display_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.display, pm_metal_display_deinit, pm_metal_display_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.display, pm_metal_display_attach, pm_metal_display_attach, int32_t(uint8_t *, uint32_t, uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.display, pm_metal_display_attach_h, pm_metal_display_attach_h, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.display, pm_metal_display_up, pm_metal_display_up, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.display, pm_metal_display_present, pm_metal_display_present, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.display, pm_metal_display_gfx_h, pm_metal_display_gfx_h, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.display, pm_metal_display_put, pm_metal_display_put, int32_t(uint32_t, uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.display, pm_metal_display_get, pm_metal_display_get, uint32_t(uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.display, pm_metal_display_width, pm_metal_display_width, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.display, pm_metal_display_height, pm_metal_display_height, uint32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.display, pm_metal_display_init, pm_metal_display_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.display, pymergetic.metal.drivers.gfx);
