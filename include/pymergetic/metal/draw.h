#ifndef PM_METAL_DRAW_H_
#define PM_METAL_DRAW_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Soft DrawSurface — pluggable later (accel = backend swap). */
typedef struct {
    uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride; /* bytes per row */
    uint8_t bpp;     /* 16 or 32 */
} pm_metal_draw_surface_t;

int32_t pm_metal_draw_soft_init(pm_metal_draw_surface_t *s, uint8_t *buf,
                                uint32_t width, uint32_t height, uint8_t bpp);

void pm_metal_draw_fill(pm_metal_draw_surface_t *s, uint32_t argb);
void pm_metal_draw_pixel(pm_metal_draw_surface_t *s, int32_t x, int32_t y, uint32_t argb);

/* 8×8 glyph from built-in font (ASCII 0x20–0x7E). */
void pm_metal_draw_glyph8(pm_metal_draw_surface_t *s, int32_t x, int32_t y,
                          char ch, uint32_t fg, uint32_t bg);

void pm_metal_draw_text8(pm_metal_draw_surface_t *s, int32_t x, int32_t y,
                         const char *text, uint32_t fg, uint32_t bg);

uint32_t pm_metal_draw_checksum(const pm_metal_draw_surface_t *s);

#ifdef __cplusplus
}
#endif

#endif
