/* Thin compositor: shadow ARGB8888 + scanout present. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/dev/gfx/gfx.h"
#include "pymergetic/metal/dev/gfx/scanout.h"
#include "pymergetic/metal/mem.h"

static pm_metal_gfx_surface_t g_surf;
static int g_ready;
static int g_heap_shadow; /* 1 = shadow from heap (free on fini) */

int pm_metal_gfx_init_from_bind(const pm_metal_scanout_bind_t *harvest)
{
    pm_metal_scanout_bind_t b;
    uint32_t *shadow;
    uint32_t pitch;
    size_t bytes;

    if (harvest == NULL || harvest->mode_w < 320u || harvest->mode_h < 200u) {
        return -1;
    }

    pm_metal_gfx_fini();

    pitch = harvest->mode_w;
    bytes = (size_t)pitch * (size_t)harvest->mode_h * sizeof(uint32_t);
    shadow = (uint32_t *)pm_metal_mem_memalign(64u, bytes);
    if (shadow == NULL) {
        shadow = (uint32_t *)pm_metal_mem_alloc(bytes);
    }
    if (shadow == NULL) {
        return -1;
    }
    memset(shadow, 0, bytes);
    g_heap_shadow = 1;

    memset(&b, 0, sizeof(b));
    b.shadow = shadow;
    b.shadow_w = harvest->mode_w;
    b.shadow_h = harvest->mode_h;
    b.shadow_pitch = pitch;
    b.fb = harvest->fb;
    b.fb_ppsl = harvest->fb_ppsl ? harvest->fb_ppsl : harvest->mode_w;
    b.mode_w = harvest->mode_w;
    b.mode_h = harvest->mode_h;
    b.gop = harvest->gop;
    b.owned = harvest->owned;

    if (pm_metal_scanout_bind(&b) != 0) {
        pm_metal_mem_free((uint8_t *)shadow);
        g_heap_shadow = 0;
        return -1;
    }

    /* DIRECT backends may adopt the back buffer. */
    {
        const pm_metal_scanout_ops_t *ops = pm_metal_scanout_ops();
        if (ops != NULL && ops->adopt_shadow != NULL &&
            (pm_metal_scanout_caps() & PM_METAL_SCANOUT_CAP_DIRECT) != 0u) {
            uint32_t *adopted = shadow;
            uint32_t apitch = pitch;
            if (ops->adopt_shadow(&adopted, &apitch) == 0) {
                if (g_heap_shadow && adopted != shadow) {
                    pm_metal_mem_free((uint8_t *)shadow);
                    g_heap_shadow = 0;
                }
                shadow = adopted;
                pitch = apitch;
            }
        }
    }

    g_surf.pixels = shadow;
    g_surf.width = b.mode_w;
    g_surf.height = b.mode_h;
    g_surf.pitch = pitch;
    g_ready = 1;
    return 0;
}

int pm_metal_gfx_init(void)
{
    return g_ready ? 0 : -1;
}

void pm_metal_gfx_fini(void)
{
    pm_metal_scanout_fini();
    if (g_heap_shadow && g_surf.pixels != NULL) {
        pm_metal_mem_free((uint8_t *)g_surf.pixels);
    }
    memset(&g_surf, 0, sizeof(g_surf));
    g_heap_shadow = 0;
    g_ready = 0;
}

int pm_metal_gfx_ready(void)
{
    return g_ready;
}

pm_metal_gfx_surface_t *pm_metal_gfx_surface(void)
{
    return g_ready ? &g_surf : NULL;
}

const char *pm_metal_gfx_scanout_name(void)
{
    return pm_metal_scanout_name();
}

void pm_metal_gfx_clear(pm_metal_gfx_color_t color)
{
    pm_metal_gfx_fill_rect(0, 0, (int32_t)g_surf.width, (int32_t)g_surf.height, color);
}

void pm_metal_gfx_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, pm_metal_gfx_color_t color)
{
    int32_t row;
    int32_t col;

    if (!g_ready || g_surf.pixels == NULL || w <= 0 || h <= 0) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= (int32_t)g_surf.width || y >= (int32_t)g_surf.height) {
        return;
    }
    if (x + w > (int32_t)g_surf.width) {
        w = (int32_t)g_surf.width - x;
    }
    if (y + h > (int32_t)g_surf.height) {
        h = (int32_t)g_surf.height - y;
    }
    for (row = 0; row < h; row++) {
        uint32_t *line = &g_surf.pixels[(uint32_t)(y + row) * g_surf.pitch + (uint32_t)x];
        for (col = 0; col < w; col++) {
            line[col] = color;
        }
    }
}

int pm_metal_gfx_present_rect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    const pm_metal_scanout_ops_t *ops;

    if (!g_ready) {
        return -1;
    }
    ops = pm_metal_scanout_ops();
    if (ops == NULL || ops->present_rect == NULL) {
        return -1;
    }
    if (w <= 0 || h <= 0) {
        x = 0;
        y = 0;
        w = (int32_t)g_surf.width;
        h = (int32_t)g_surf.height;
    }
    return ops->present_rect(x, y, w, h);
}

int pm_metal_gfx_present(void)
{
    return pm_metal_gfx_present_rect(0, 0, 0, 0);
}
