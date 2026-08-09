#ifndef PYMERGETIC_METAL_DEV_GFX_TEXT_H_
#define PYMERGETIC_METAL_DEV_GFX_TEXT_H_
#include <stdint.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#ifdef __cplusplus
extern "C" {
#endif
void pm_metal_gfx_draw_text(int32_t x, int32_t y, const char *text, pm_metal_gfx_color_t fg,
                            pm_metal_gfx_color_t bg, int transparent_bg);
uint32_t pm_metal_gfx_font_width(void);
uint32_t pm_metal_gfx_font_height(void);
#ifdef __cplusplus
}
#endif
#endif
