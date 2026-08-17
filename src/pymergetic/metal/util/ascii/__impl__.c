/* pymergetic.metal.util.ascii — FIGlet "small" (Glenn Chappell / BSD-3). */
#include "pymergetic/metal/util/ascii/__exports__.h"
#include "pymergetic/metal/util/ascii/__types__.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pymergetic/metal/console.h"

#include "fig_small.inc"

#define PM_METAL_ASCII_MAX_W 160
#define PM_METAL_ASCII_MAX_H 32
#define PM_METAL_ASCII_RAINBOW_STEPS 16u
#define PM_METAL_ASCII_RAINBOW_ROWDEG 40u

static const pm_metal_ascii_fig_glyph_t *ascii_glyph(unsigned char ch) {
    if (ch < PM_METAL_ASCII_FIG_FIRST || ch >= PM_METAL_ASCII_FIG_FIRST + PM_METAL_ASCII_FIG_COUNT) {
        ch = (unsigned char)'?';
    }
    return &mFigFont[ch - PM_METAL_ASCII_FIG_FIRST];
}

static int ascii_glyph_width(const pm_metal_ascii_fig_glyph_t *g) {
    int w = 0;
    int r;
    int c;
    for (r = 0; r < PM_METAL_ASCII_FIG_H; r++) {
        c = (int)strlen(g->row[r]);
        while (c > 0 && g->row[r][c - 1] == ' ') {
            c--;
        }
        if (c > w) {
            w = c;
        }
    }
    return w;
}

static void emit_line(const char *s) {
    uint32_t n = 0;
    if (s == NULL) {
        s = "";
    }
    while (s[n] != 0) {
        n++;
    }
    (void)pm_metal_console_write(s, n);
    (void)pm_metal_console_write("\n", 1);
}

static const char *style_sgr(int32_t style) {
    switch (style) {
    case PM_METAL_UTIL_ASCII_STYLE_DIM:
        return "\033[2m";
    case PM_METAL_UTIL_ASCII_STYLE_OK:
        return "\033[32m";
    case PM_METAL_UTIL_ASCII_STYLE_WARN:
        return "\033[33m";
    case PM_METAL_UTIL_ASCII_STYLE_FAIL:
        return "\033[31m";
    case PM_METAL_UTIL_ASCII_STYLE_ACCENT:
        return "\033[36m";
    default:
        return "";
    }
}

static void hue_rgb(uint32_t deg, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint32_t region;
    uint32_t rem;
    uint8_t rising;
    uint8_t falling;
    deg = deg % 360u;
    region = deg / 60u;
    rem = (deg % 60u) * 255u / 60u;
    rising = (uint8_t)rem;
    falling = (uint8_t)(255u - rem);
    switch (region) {
    case 0:
        *r = 255u;
        *g = rising;
        *b = 0u;
        break;
    case 1:
        *r = falling;
        *g = 255u;
        *b = 0u;
        break;
    case 2:
        *r = 0u;
        *g = 255u;
        *b = rising;
        break;
    case 3:
        *r = 0u;
        *g = falling;
        *b = 255u;
        break;
    case 4:
        *r = rising;
        *g = 0u;
        *b = 255u;
        break;
    default:
        *r = 255u;
        *g = 0u;
        *b = falling;
        break;
    }
}

static size_t colorize_row(const char *row, uint32_t phase_deg, char *out, size_t out_cap) {
    size_t len;
    size_t x;
    size_t oi;
    int32_t last_step;
    if (out == NULL || out_cap == 0u) {
        return 0;
    }
    len = strlen(row);
    oi = 0;
    last_step = -1;
    for (x = 0; x < len; x++) {
        char c = row[x];
        if (c != ' ') {
            uint32_t step = (uint32_t)(x * PM_METAL_ASCII_RAINBOW_STEPS / (len > 0u ? len : 1u));
            if ((int32_t)step != last_step) {
                uint32_t deg = (step * 360u / PM_METAL_ASCII_RAINBOW_STEPS + phase_deg) % 360u;
                uint8_t r;
                uint8_t g;
                uint8_t b;
                int n;
                hue_rgb(deg, &r, &g, &b);
                n = snprintf(out + oi, (oi < out_cap) ? out_cap - oi : 0u, "\033[38;2;%u;%u;%um",
                    (unsigned)r, (unsigned)g, (unsigned)b);
                if (n > 0) {
                    oi += (size_t)n;
                }
                last_step = (int32_t)step;
            }
        }
        if (oi + 1u < out_cap) {
            out[oi++] = c;
        }
    }
    {
        int n = snprintf(out + oi, (oi < out_cap) ? out_cap - oi : 0u, "\033[0m");
        if (n > 0) {
            oi += (size_t)n;
        }
    }
    if (oi >= out_cap) {
        oi = out_cap - 1u;
    }
    out[oi] = '\0';
    return oi;
}

size_t pm_metal_util_ascii_bound(size_t text_len) {
    size_t w;
    size_t h;
    if (text_len == 0) {
        return 1;
    }
    w = text_len * (size_t)PM_METAL_ASCII_FIG_MAX_W;
    h = (size_t)PM_METAL_ASCII_FIG_H * (text_len + 1u);
    if (w > (size_t)PM_METAL_ASCII_MAX_W) {
        w = (size_t)PM_METAL_ASCII_MAX_W;
    }
    if (h > (size_t)PM_METAL_ASCII_MAX_H) {
        h = (size_t)PM_METAL_ASCII_MAX_H;
    }
    return h * (w + 1u) + 1u;
}

int32_t pm_metal_util_ascii_render(const char *text, char ink, char *out, size_t out_cap) {
    static char lines[PM_METAL_ASCII_FIG_H][PM_METAL_ASCII_MAX_W + 1];
    int lens[PM_METAL_ASCII_FIG_H];
    int r;
    int c;
    int end;
    size_t o;
    const char *p;
    (void)ink;
    if (!text || !out || out_cap == 0) {
        return -1;
    }
    for (r = 0; r < PM_METAL_ASCII_FIG_H; r++) {
        lines[r][0] = '\0';
        lens[r] = 0;
    }
    for (p = text; *p != '\0'; p++) {
        const pm_metal_ascii_fig_glyph_t *g;
        int gw;
        if (*p == '\n') {
            continue;
        }
        g = ascii_glyph((unsigned char)*p);
        gw = ascii_glyph_width(g);
        for (r = 0; r < PM_METAL_ASCII_FIG_H; r++) {
            if (lens[r] + gw > PM_METAL_ASCII_MAX_W) {
                return -1;
            }
            for (c = 0; c < gw; c++) {
                char ch = g->row[r][c];
                lines[r][lens[r] + c] = (ch != '\0') ? ch : ' ';
            }
            lens[r] += gw;
            lines[r][lens[r]] = '\0';
        }
    }
    end = PM_METAL_ASCII_FIG_H - 1;
    while (end >= 0) {
        c = lens[end];
        while (c > 0 && lines[end][c - 1] == ' ') {
            c--;
        }
        if (c > 0) {
            break;
        }
        end--;
    }
    o = 0;
    for (r = 0; r <= end; r++) {
        c = lens[r];
        while (c > 0 && lines[r][c - 1] == ' ') {
            c--;
        }
        if (o + (size_t)c + 1u >= out_cap) {
            return -1;
        }
        memcpy(out + o, lines[r], (size_t)c);
        o += (size_t)c;
        if (r < end) {
            out[o++] = '\n';
        }
    }
    if (o >= out_cap) {
        return -1;
    }
    out[o] = '\0';
    return (int32_t)o;
}

void pm_metal_util_ascii_log_styled(int32_t style, const char *text) {
    char out[PM_METAL_ASCII_MAX_H * (PM_METAL_ASCII_MAX_W + 1) + 1];
    char painted[PM_METAL_ASCII_MAX_W + 16];
    const char *sgr;
    int32_t n;
    int i;
    int start;
    if (!text) {
        return;
    }
    n = pm_metal_util_ascii_render(text, '#', out, sizeof(out));
    if (n < 0) {
        return;
    }
    sgr = style_sgr(style);
    start = 0;
    for (i = 0; i <= n; i++) {
        if (i == n || out[i] == '\n') {
            out[i] = '\0';
            if (sgr[0] != '\0') {
                snprintf(painted, sizeof(painted), "%s%s\033[0m", sgr, out + start);
                emit_line(painted);
            } else {
                emit_line(out + start);
            }
            start = i + 1;
        }
    }
}

void pm_metal_util_ascii_log(const char *text) {
    pm_metal_util_ascii_log_styled(PM_METAL_UTIL_ASCII_STYLE_DEFAULT, text);
}

void pm_metal_util_ascii_log_rainbow(const char *text) {
    char out[PM_METAL_ASCII_MAX_H * (PM_METAL_ASCII_MAX_W + 1) + 1];
    char colored[PM_METAL_ASCII_MAX_W * 20 + 8];
    int32_t n;
    int i;
    int start;
    int row;
    if (!text) {
        return;
    }
    n = pm_metal_util_ascii_render(text, '#', out, sizeof(out));
    if (n < 0) {
        return;
    }
    start = 0;
    row = 0;
    for (i = 0; i <= n; i++) {
        if (i == n || out[i] == '\n') {
            out[i] = '\0';
            colorize_row(out + start, (uint32_t)row * PM_METAL_ASCII_RAINBOW_ROWDEG, colored,
                sizeof(colored));
            emit_line(colored);
            start = i + 1;
            row++;
        }
    }
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.util.ascii, pm_metal_util_ascii_bound, pm_metal_util_ascii_bound, size_t(size_t));
PM_MOD_EXPORT_C(pymergetic.metal.util.ascii, pm_metal_util_ascii_render, pm_metal_util_ascii_render, int32_t(const char *, char, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.util.ascii, pm_metal_util_ascii_log, pm_metal_util_ascii_log, void(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.util.ascii, pm_metal_util_ascii_log_styled, pm_metal_util_ascii_log_styled, void(int32_t, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.util.ascii, pm_metal_util_ascii_log_rainbow, pm_metal_util_ascii_log_rainbow, void(const char *));
