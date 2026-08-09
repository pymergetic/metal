/*
 * Metal graphics upper half — shadow surface + present via scanout.
 * Physical backends: scanout.h
 */
#ifndef PYMERGETIC_METAL_DEV_GFX_GFX_H_
#define PYMERGETIC_METAL_DEV_GFX_GFX_H_

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/dev/gfx/scanout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pm_metal_gfx_color_t;

#define PM_METAL_GFX_RGBA(r, g, b, a)                                                \
    ((pm_metal_gfx_color_t)(((uint32_t)(b) & 0xffu) | (((uint32_t)(g) & 0xffu) << 8) | \
                            (((uint32_t)(r) & 0xffu) << 16) | (((uint32_t)(a) & 0xffu) << 24)))

#define PM_METAL_GFX_RGB(r, g, b) PM_METAL_GFX_RGBA((r), (g), (b), 0xff)

typedef struct {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch; /* pixels per row */
} pm_metal_gfx_surface_t;

/* Fill bind from board harvest (GOP / VESA / Bochs), then scanout_bind + shadow. */
int pm_metal_gfx_init_from_bind(const pm_metal_scanout_bind_t *harvest);
int pm_metal_gfx_init(void);
void pm_metal_gfx_fini(void);
int pm_metal_gfx_ready(void);

pm_metal_gfx_surface_t *pm_metal_gfx_surface(void);
const char *pm_metal_gfx_scanout_name(void);

void pm_metal_gfx_clear(pm_metal_gfx_color_t color);
void pm_metal_gfx_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, pm_metal_gfx_color_t color);
void pm_metal_gfx_draw_text(int32_t x, int32_t y, const char *text, pm_metal_gfx_color_t fg,
                            pm_metal_gfx_color_t bg, int transparent_bg);

/* Present dirty rect (or full surface if w/h <= 0). */
int pm_metal_gfx_present_rect(int32_t x, int32_t y, int32_t w, int32_t h);
int pm_metal_gfx_present(void);

uint32_t pm_metal_gfx_font_width(void);
uint32_t pm_metal_gfx_font_height(void);

#ifdef __cplusplus
}
#endif

#endif
