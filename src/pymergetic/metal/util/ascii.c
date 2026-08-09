/*
 * pm_metal_util_ascii_* — impl: common (see util/ascii.h; wasm32 mods
 * reach this via wasi-style import registration at the bottom).
 *
 * Classic FIGlet "small" letterforms (Glenn Chappell / FIGlet fonts,
 * freely redistributable) — not solid '#' blobs from the VGA pixel
 * font. ~5 rows tall (~1/3 of the old 16-row raster banners).
 */
#include "pymergetic/metal/util/ascii.h"

#include "pymergetic/metal/boot/tree.h"
#include <pymergetic/metal/reg/mod.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ascii_fig_small.inc.c"

static pm_metal_reg_export_t util_ascii_exports[] = {
    PM_METAL_REG_EXPORT(bound),
    PM_METAL_REG_EXPORT(render),
    PM_METAL_REG_EXPORT(log),
    PM_METAL_REG_EXPORT(log_cyan),
    PM_METAL_REG_EXPORT(log_rainbow),
};
PM_METAL_REG_REF(util_ascii, bound, 0);
PM_METAL_REG_REF(util_ascii, render, 1);
PM_METAL_REG_REF(util_ascii, log, 2);
PM_METAL_REG_REF(util_ascii, log_cyan, 3);
PM_METAL_REG_REF(util_ascii, log_rainbow, 4);
PM_METAL_REG_MOD(util_ascii, "pymergetic.metal.util.ascii")

static int32_t util_ascii_register_symbols(void *ctx)
{
  (void)ctx;
  pm_metal_reg_export_publish(util_ascii_bound, (void *)pm_metal_util_ascii_bound);
  pm_metal_reg_export_publish(util_ascii_render, (void *)pm_metal_util_ascii_render);
  pm_metal_reg_export_publish(util_ascii_log, (void *)pm_metal_util_ascii_log);
  pm_metal_reg_export_publish(util_ascii_log_cyan, (void *)pm_metal_util_ascii_log_cyan);
  pm_metal_reg_export_publish(util_ascii_log_rainbow, (void *)pm_metal_util_ascii_log_rainbow);
  return 0;
}

#define PM_METAL_ASCII_MAX_W 160
#define PM_METAL_ASCII_MAX_H 32

static const pm_metal_ascii_fig_glyph_t *metal_ascii_glyph(unsigned char ch)
{
  if (ch < PM_METAL_ASCII_FIG_FIRST || ch >= PM_METAL_ASCII_FIG_FIRST + PM_METAL_ASCII_FIG_COUNT) {
    ch = (unsigned char)'?';
  }
  return &mFigFont[ch - PM_METAL_ASCII_FIG_FIRST];
}

static int metal_ascii_glyph_width(const pm_metal_ascii_fig_glyph_t *g)
{
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

size_t pm_metal_util_ascii_bound(size_t text_len)
{
  size_t w;
  size_t h;

  if (text_len == 0) {
    return 1;
  }

  /* worst case: each char at max glyph width + newlines */
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

int pm_metal_util_ascii_render(const char *text, char ink, char *out, size_t out_cap)
{
  static char lines[PM_METAL_ASCII_FIG_H][PM_METAL_ASCII_MAX_W + 1];
  int         lens[PM_METAL_ASCII_FIG_H];
  int         r;
  int         c;
  int         end;
  size_t      o;
  const char *p;

  (void)ink; /* FIGlet glyphs already carry their own ink chars */

  if (!text || !out || out_cap == 0) {
    return -1;
  }

  for (r = 0; r < PM_METAL_ASCII_FIG_H; r++) {
    lines[r][0] = '\0';
    lens[r]     = 0;
  }

  for (p = text; *p != '\0'; p++) {
    const pm_metal_ascii_fig_glyph_t *g;
    int                               gw;

    if (*p == '\n') {
      /* only single-line banners for now — ignore extra */
      continue;
    }

    g  = metal_ascii_glyph((unsigned char)*p);
    gw = metal_ascii_glyph_width(g);
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

  /* trim trailing blank fig rows (small font often pads last line) */
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
  return (int)o;
}

void pm_metal_util_ascii_log(const char *text)
{
  char out[PM_METAL_ASCII_MAX_H * (PM_METAL_ASCII_MAX_W + 1) + 1];
  int  n;
  int  i;
  int  start;

  if (!text) {
    return;
  }

  n = pm_metal_util_ascii_render(text, '#', out, sizeof(out));
  if (n < 0) {
    return;
  }

  start = 0;
  for (i = 0; i <= n; i++) {
    if (i == n || out[i] == '\n') {
      out[i] = '\0';
      pm_metal_boot_emit(out + start);
      start = i + 1;
    }
  }
}

void pm_metal_util_ascii_log_cyan(const char *text)
{
  char out[PM_METAL_ASCII_MAX_H * (PM_METAL_ASCII_MAX_W + 1) + 1];
  char line[PM_METAL_ASCII_MAX_W + 32];
  int  n;
  int  i;
  int  start;

  if (!text) {
    return;
  }

  n = pm_metal_util_ascii_render(text, '#', out, sizeof(out));
  if (n < 0) {
    return;
  }

  start = 0;
  for (i = 0; i <= n; i++) {
    if (i == n || out[i] == '\n') {
      out[i] = '\0';
      snprintf(line, sizeof(line), "\033[36m%s\033[0m", out + start);
      pm_metal_boot_emit(line);
      start = i + 1;
    }
  }
}

/* Hue wheel steps swept across each row's glyph columns; kept small so a
 * whole banner row's worth of "\033[38;2;r;g;bm" escapes (~19 bytes each,
 * emitted once per step, not once per char) fits the console's per-line
 * storage budget with room to spare (see shell/ui/priv.h's CONSOLE_COLS). */
#define PM_METAL_ASCII_RAINBOW_STEPS  16u
#define PM_METAL_ASCII_RAINBOW_ROWDEG 40u /* per-row hue phase -- the "diagonal" */

/* Integer HSV(deg, S=255, V=255) -> RGB -- no floats/trig (freestanding
 * boot code, deg is 0-359). Classic 6-sextant wheel. */
static void AsciiHueToRgb(uint32_t deg, uint8_t *r, uint8_t *g, uint8_t *b)
{
  uint32_t region;
  uint32_t rem;
  uint8_t  rising;
  uint8_t  falling;

  deg     = deg % 360u;
  region  = deg / 60u;
  rem     = (deg % 60u) * 255u / 60u;
  rising  = (uint8_t)rem;
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

/**
 * Colorize one already-rendered FIGlet row: a new 24-bit SGR escape is
 * emitted each time the hue step changes (every ~row_len/STEPS chars, not
 * every char -- keeps escape overhead bounded regardless of banner width),
 * spaces pass through uncolored (invisible either way), row ends with a
 * plain reset. Returns bytes written (excluding NUL), out always NUL
 * terminated.
 */
static size_t AsciiColorizeRow(const char *row, uint32_t phase_deg, char *out, size_t out_cap)
{
  size_t  len;
  size_t  x;
  size_t  oi;
  int32_t last_step;

  if (out == NULL || out_cap == 0u) {
    return 0;
  }

  len       = strlen(row);
  oi        = 0;
  last_step = -1;

  for (x = 0; x < len; x++) {
    char c = row[x];

    if (c != ' ') {
      uint32_t step = (uint32_t)(x * PM_METAL_ASCII_RAINBOW_STEPS / (len > 0u ? len : 1u));

      if ((int32_t)step != last_step) {
        uint32_t deg = (step * 360u / PM_METAL_ASCII_RAINBOW_STEPS + phase_deg) % 360u;
        uint8_t  r;
        uint8_t  g;
        uint8_t  b;
        int      n;

        AsciiHueToRgb(deg, &r, &g, &b);
        n = snprintf(out + oi,
                     (oi < out_cap) ? out_cap - oi : 0u,
                     "\033[38;2;%u;%u;%um",
                     (unsigned)r,
                     (unsigned)g,
                     (unsigned)b);
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

void pm_metal_util_ascii_log_rainbow(const char *text)
{
  char out[PM_METAL_ASCII_MAX_H * (PM_METAL_ASCII_MAX_W + 1) + 1];
  char colored[PM_METAL_ASCII_MAX_W * 20 + 8];
  int  n;
  int  i;
  int  start;
  int  row;

  if (!text) {
    return;
  }

  n = pm_metal_util_ascii_render(text, '#', out, sizeof(out));
  if (n < 0) {
    return;
  }

  start = 0;
  row   = 0;
  for (i = 0; i <= n; i++) {
    if (i == n || out[i] == '\n') {
      out[i] = '\0';
      AsciiColorizeRow(
        out + start, (uint32_t)row * PM_METAL_ASCII_RAINBOW_ROWDEG, colored, sizeof(colored));
      pm_metal_boot_emit(colored);
      start = i + 1;
      row++;
    }
  }
}
