/* 8×16 VGA font text on compositor shadow. */
#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/dev/gfx/gfx.h"

#include "font_vga8x16.inc.c"

#include <pymergetic/metal/reg/mod.h>

/* RegMod declare (C SoT) — loaded via pm_metal_dev_gfx_text_reg_load. */
static pm_metal_reg_export_t dev_gfx_text_exports[] = {
    PM_METAL_REG_EXPORT(draw_text),
    PM_METAL_REG_EXPORT(font_width),
    PM_METAL_REG_EXPORT(font_height),
};
PM_METAL_REG_REF(dev_gfx_text, draw_text, 0);
PM_METAL_REG_REF(dev_gfx_text, font_width, 1);
PM_METAL_REG_REF(dev_gfx_text, font_height, 2);
PM_METAL_REG_MOD(dev_gfx_text, "pymergetic.metal.dev.gfx.text")

static int32_t dev_gfx_text_register_symbols(void *ctx)
{
    (void)ctx;
    pm_metal_reg_export_publish(dev_gfx_text_draw_text, (void *)pm_metal_gfx_draw_text);
    pm_metal_reg_export_publish(dev_gfx_text_font_width, (void *)pm_metal_gfx_font_width);
    pm_metal_reg_export_publish(dev_gfx_text_font_height, (void *)pm_metal_gfx_font_height);
    return 0;
}
uint32_t pm_metal_gfx_font_width(void)
{
    return (uint32_t)PM_METAL_GFX_FONT_W;
}

uint32_t pm_metal_gfx_font_height(void)
{
    return (uint32_t)PM_METAL_GFX_FONT_H;
}

void pm_metal_gfx_draw_text(int32_t x, int32_t y, const char *text, pm_metal_gfx_color_t fg,
                            pm_metal_gfx_color_t bg, int transparent_bg)
{
    pm_metal_gfx_surface_t *s;
    int32_t cx;
    const unsigned char *p;

    s = pm_metal_gfx_surface();
    if (s == NULL || s->pixels == NULL || text == NULL) {
        return;
    }

    cx = x;
    for (p = (const unsigned char *)text; *p != 0; p++) {
        unsigned char ch = *p;
        const uint8_t *glyph;
        int32_t row;
        int32_t col;

        glyph = &mFontGlyphs[(size_t)ch * (size_t)PM_METAL_GFX_FONT_BYTES_PER_GLYPH];
        for (row = 0; row < PM_METAL_GFX_FONT_H; row++) {
            uint8_t bits = glyph[row];
            uint32_t *line;
            int32_t py = y + row;
            if (py < 0 || py >= (int32_t)s->height) {
                continue;
            }
            line = &s->pixels[(uint32_t)py * s->pitch];
            for (col = 0; col < PM_METAL_GFX_FONT_W; col++) {
                int32_t px = cx + col;
                if (px < 0 || px >= (int32_t)s->width) {
                    continue;
                }
                if ((bits & (uint8_t)(0x80u >> col)) != 0u) {
                    line[px] = fg;
                } else if (!transparent_bg) {
                    line[px] = bg;
                }
            }
        }
        cx += PM_METAL_GFX_FONT_W;
    }
}
