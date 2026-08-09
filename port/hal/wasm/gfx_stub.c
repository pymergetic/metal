/*
 * Browser gfx + scanout — same C ABI; no physical framebuffer.
 */
#include "pymergetic/metal/dev/gfx/gfx.h"
#include "pymergetic/metal/dev/gfx/scanout.h"

#include <stddef.h>
#include <string.h>

static pm_metal_scanout_bind_t g_bind;
static int g_bound;

int pm_metal_gfx_init_from_bind(const pm_metal_scanout_bind_t *harvest)
{
    (void)harvest;
    return -1;
}

int pm_metal_gfx_init(void) { return -1; }

void pm_metal_gfx_fini(void) {}

int pm_metal_gfx_ready(void) { return 0; }

pm_metal_gfx_surface_t *pm_metal_gfx_surface(void) { return NULL; }

const char *pm_metal_gfx_scanout_name(void) { return "wasm-none"; }

void pm_metal_gfx_clear(pm_metal_gfx_color_t color) { (void)color; }

void pm_metal_gfx_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, pm_metal_gfx_color_t color)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
}

void pm_metal_gfx_draw_text(int32_t x, int32_t y, const char *text, pm_metal_gfx_color_t fg,
                            pm_metal_gfx_color_t bg, int transparent_bg)
{
    (void)x;
    (void)y;
    (void)text;
    (void)fg;
    (void)bg;
    (void)transparent_bg;
}

int pm_metal_gfx_present_rect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return -1;
}

int pm_metal_gfx_present(void) { return -1; }

uint32_t pm_metal_gfx_font_width(void) { return 8u; }

uint32_t pm_metal_gfx_font_height(void) { return 16u; }

int pm_metal_scanout_bind(const pm_metal_scanout_bind_t *b)
{
    if (b) {
        g_bind = *b;
        g_bound = 1;
    }
    return -1;
}

const pm_metal_scanout_ops_t *pm_metal_scanout_ops(void) { return NULL; }

const char *pm_metal_scanout_name(void) { return "wasm-none"; }

uint32_t pm_metal_scanout_caps(void) { return 0; }

void pm_metal_scanout_fini(void)
{
    memset(&g_bind, 0, sizeof(g_bind));
    g_bound = 0;
}

const pm_metal_scanout_bind_t *pm_metal_scanout_bind_info(void)
{
    return g_bound ? &g_bind : NULL;
}

void pm_metal_scanout_bind_set_shadow(uint32_t *pixels, uint32_t pitch)
{
    g_bind.shadow = pixels;
    g_bind.shadow_pitch = pitch;
    g_bound = 1;
}

void pm_metal_scanout_copy_rect(uint32_t *dst, uint32_t dst_pitch, int32_t x, int32_t y, int32_t w,
                                int32_t h, const pm_metal_scanout_bind_t *b)
{
    (void)dst;
    (void)dst_pitch;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)b;
}
