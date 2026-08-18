/* pymergetic.metal.console — "fb" viewport: 8x16 glyphs onto display. */
#include "pymergetic/metal/console/__exports__.h"
#include "pymergetic/metal/console/__types__.h"

#include "pymergetic/metal/display.h"

#include <stdint.h>
#include <string.h>

#define FB_GLYPH_W 8u
#define FB_GLYPH_H 16u
#define FB_FG 0x00e0e0e0u
#define FB_BG 0x00000000u

static uint32_t s_col;
static uint32_t s_row;
static uint32_t s_on;

/* 8x16, bit 7 is leftmost. Printable box; A / F / 1 are drawn. */
static const uint8_t s_font[128][16] = {
    [32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    ['1'] = {0, 0, 0x08, 0x18, 0x28, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x3e, 0, 0, 0, 0},
    ['A'] = {0, 0, 0x18, 0x24, 0x24, 0x42, 0x42, 0x7e, 0x42, 0x42, 0x42, 0x42, 0, 0, 0, 0},
    ['F'] = {0, 0, 0x7e, 0x40, 0x40, 0x40, 0x7c, 0x40, 0x40, 0x40, 0x40, 0x40, 0, 0, 0, 0},
};

static const uint8_t *glyph(unsigned char ch) {
    static const uint8_t box[16] = {
        0, 0, 0x7e, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x7e, 0, 0, 0, 0
    };
    if (ch > 127u) {
        return box;
    }
    if (ch == ' ' || s_font[ch][2] != 0 || s_font[ch][7] != 0) {
        return s_font[ch];
    }
    if (ch >= 33u && ch < 127u) {
        return box;
    }
    return s_font[32];
}

static void blit_glyph(uint32_t col, uint32_t row, unsigned char ch) {
    const uint8_t *g = glyph(ch);
    uint32_t x0 = col * FB_GLYPH_W;
    uint32_t y0 = row * FB_GLYPH_H;
    uint32_t r;
    uint32_t c;
    uint32_t dw = pm_metal_display_width();
    uint32_t dh = pm_metal_display_height();
    if (x0 + FB_GLYPH_W > dw || y0 + FB_GLYPH_H > dh) {
        return;
    }
    for (r = 0; r < FB_GLYPH_H; r++) {
        uint8_t bits = g[r];
        for (c = 0; c < FB_GLYPH_W; c++) {
            uint32_t rgb = (bits & (uint8_t)(0x80u >> c)) != 0 ? FB_FG : FB_BG;
            (void)pm_metal_display_put(x0 + c, y0 + r, rgb);
        }
    }
}

static void fb_sink(const char *s, uint32_t n) {
    uint32_t i;
    uint32_t cols;
    uint32_t rows;
    if (s == NULL || n == 0 || !s_on) {
        return;
    }
    cols = pm_metal_display_width() / FB_GLYPH_W;
    rows = pm_metal_display_height() / FB_GLYPH_H;
    if (cols == 0 || rows == 0) {
        return;
    }
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            s_col = 0;
            if (s_row + 1u < rows) {
                s_row++;
            }
            continue;
        }
        blit_glyph(s_col, s_row, (unsigned char)c);
        s_col++;
        if (s_col >= cols) {
            s_col = 0;
            if (s_row + 1u < rows) {
                s_row++;
            }
        }
    }
    (void)pm_metal_display_present();
}

int32_t pm_metal_console_fb_attach(void) {
    int32_t id;
    uint32_t i;
    if (pm_metal_display_width() < FB_GLYPH_W || pm_metal_display_height() < FB_GLYPH_H) {
        return -1;
    }
    for (id = 0; id < (int32_t)pm_metal_console_count(); id++) {
        for (i = 0; i < pm_metal_console_viewport_count_id(id); i++) {
            const char *k = pm_metal_console_viewport_kind_id(id, i);
            if (k != NULL && k[0] == 'f' && k[1] == 'b' && k[2] == 0) {
                return 0;
            }
        }
    }
    s_col = 0;
    s_row = 0;
    s_on = 1;
    if (pm_metal_console_viewport_attach("fb", fb_sink) != 0) {
        s_on = 0;
        return -1;
    }
    return 0;
}

