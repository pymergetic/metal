/* pymergetic.metal.display — RGB888 pixels in a caller buffer. */
#include "pymergetic/metal/display/__exports__.h"

#include <string.h>

static pm_util_mem_arena_t *s_arena;
static uint8_t *s_pix;
static uint32_t s_w;
static uint32_t s_h;
static uint32_t s_stride;

int32_t pm_metal_display_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    s_pix = NULL;
    s_w = 0;
    s_h = 0;
    s_stride = 0;
    return 0;
}

void pm_metal_display_deinit(void) {
    s_pix = NULL;
    s_w = 0;
    s_h = 0;
    s_stride = 0;
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
    return 0;
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
PM_MOD_EXPORT_C(pymergetic.metal.display, pm_metal_display_put, pm_metal_display_put, int32_t(uint32_t, uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.display, pm_metal_display_get, pm_metal_display_get, uint32_t(uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.display, pm_metal_display_width, pm_metal_display_width, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.display, pm_metal_display_height, pm_metal_display_height, uint32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.display, pm_metal_display_init, pm_metal_display_deinit);
