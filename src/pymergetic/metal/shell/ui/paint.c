/** @file
  UI layout + paint (window chrome, console, status tray/clock).
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "priv.h"

#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/dev/audio/audio.h>
#include <pymergetic/metal/net/ip/ip_cfg.h>
#include <pymergetic/metal/net/ntp/ntp.h>
#include <pymergetic/metal/dev/random/random.h>
#include <pymergetic/metal/guest/wasm/wasm.h>
#include <pymergetic/metal/guest/process/process.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/host/host.h>
#include <pymergetic/metal/util/ip.h>

static uint32_t mStatusClockTod  = 0xffffffffu;
static uint32_t mStatusNetHealth = 0xffffffffu;
static uint32_t mStatusIfCount   = 0xffffffffu;
static uint32_t mStatusNtpBit    = 0xffffffffu;
static uint32_t mStatusFpsHz     = 0xffffffffu;
static uint32_t mStatusKeyb      = 0xffffffffu;
static uint32_t mStatusAudPacked = 0xffffffffu; /* ready:1 mute:1 vol:8 */

#define UI_AUD_SLIDER_W 40

/* Hit regions for MetalUiStatusAudioPointer (updated each status paint). */
static int32_t mAudValid;
static int32_t mAudY;
static int32_t mAudH;
static int32_t mAudMuteX;
static int32_t mAudMuteW;
static int32_t mAudSlideX;
static int32_t mAudSlideW;

/* Per-iface tray color: 0=down, 1=partial (IP no DNS), 2=good — packed 2 bits. */
#define NET_HEALTH_DOWN    0u
#define NET_HEALTH_PARTIAL 1u
#define NET_HEALTH_GOOD    2u

/**
 * True when a live wasm session draws into this tab's surface (windowed
 * guest). Chrome must not fill that content rect or the game vanishes.
 * Fullscreen (`run`) draws DEFAULT — not a tab content owner (and must not
 * keep painting the shared input strip over the game).
 */
static int32_t MetalUiTabGuestOwnsContent(metal_ui_widget_t *w)
{
  metal_ui_widget_t    *tab;
  pm_metal_process_id_t pid;

  tab = w;
  while (tab != NULL && tab->kind != METAL_UI_KIND_TAB) {
    tab = tab->parent;
  }

  if (tab == NULL || tab->surface == PM_METAL_GFX_SURFACE_INVALID) {
    return 0;
  }

  if (!pm_metal_process_active()) {
    return 0;
  }

  pid = pm_metal_process_current();
  if (pid != PM_METAL_PROCESS_ID_INVALID &&
      pm_metal_process_ui_kind(pid) == PM_METAL_PROC_UI_FULLSCREEN) {
    return 0;
  }

  return pm_metal_wasm_stdout_tab() == tab->handle;
}

static void MetalUiLayoutWindow(metal_ui_widget_t *win, int32_t sw, int32_t sh)
{
  metal_ui_widget_t *tabs;
  metal_ui_widget_t *st;
  metal_ui_widget_t *tab;
  metal_ui_widget_t *frame;
  metal_ui_widget_t *con;
  uint32_t           i;
  int32_t            x;
  int32_t            y;
  int32_t            w;
  int32_t            h;
  int32_t            body_y;
  int32_t            body_h;

  x = UI_MARGIN;
  y = UI_MARGIN;
  w = sw - 2 * UI_MARGIN;
  h = sh - 2 * UI_MARGIN;
  if (w < 160 || h < 120 || gMetalUiTabs == NULL || gMetalUiStatus == NULL) {
    return;
  }

  win->x = x;
  win->y = y;
  win->w = w;
  win->h = h;

  tabs = gMetalUiTabs;
  st   = gMetalUiStatus;

  tabs->x = x + 4;
  tabs->y = y + UI_TITLE_H;
  tabs->w = w - 8;
  tabs->h = UI_TAB_H;

  st->x = x + 4;
  st->y = y + h - UI_STATUS_H - 4;
  st->w = w - 8;
  st->h = UI_STATUS_H;

  body_y = tabs->y + tabs->h + 2;
  body_h = st->y - body_y - 2;

  for (i = 0; i < tabs->u.tabs.n; i++) {
    tab = tabs->u.tabs.tabs[i];
    if (tab == NULL) {
      continue;
    }

    tab->x = tabs->x;
    tab->y = body_y;
    tab->w = tabs->w;
    tab->h = body_h;

    frame = tab->child;
    if (frame == NULL) {
      continue;
    }

    frame->x = tab->x;
    frame->y = tab->y;
    frame->w = tab->w;
    frame->h = tab->h;

    con = frame->child;
    if (con == NULL) {
      continue;
    }

    /*
     * Whole tab is one scrollable console — the composing line is just
     * its trailing row(s) (MetalUiPaintConsole + MetalUiConsoleTotalRows),
     * not a separate strip with its own geometry/divider/scrollbar. The
     * console always gets the full padded frame interior.
     */
    con->x = frame->x + UI_FRAME_PAD;
    con->y = frame->y + UI_FRAME_PAD;
    con->w = frame->w - 2 * UI_FRAME_PAD;
    con->h = frame->h - 2 * UI_FRAME_PAD;

    if (tab->surface != PM_METAL_GFX_SURFACE_INVALID) {
      /* Guest = padded frame interior (no prompt strip while playing). */
      pm_metal_gfx_surface_set_rect(tab->surface, con->x, con->y, con->w, con->h);
    }
  }
}

/**
 * Draw text with a minimal CSI SGR subset (same codes log/prompt emit).
 * Advances at most max_cols glyph cells; escape bytes do not consume cells.
 */
static void MetalUiDrawTextAnsi(
  int32_t x, int32_t y, const char *text, pm_metal_gfx_color_t def_fg, uint32_t max_cols)
{
  const char          *p;
  int32_t              cx;
  uint32_t             cols;
  uint32_t             fw;
  pm_metal_gfx_color_t fg;
  char                 ch[2];

  if (text == NULL || max_cols == 0u) {
    return;
  }

  fw = pm_metal_gfx_font_width();
  if (fw == 0u) {
    fw = UI_FONT_W;
  }

  p     = text;
  cx    = x;
  cols  = 0;
  fg    = def_fg;
  ch[1] = '\0';

  while (*p != '\0' && cols < max_cols) {
    if (p[0] == '\033' && p[1] == '[') {
      p += 2;
      while (*p != '\0' && *p != 'm') {
        uint32_t v;

        if (*p < '0' || *p > '9') {
          p++;
          continue;
        }

        v = 0;
        while (*p >= '0' && *p <= '9') {
          v = v * 10u + (uint32_t)(*p - '0');
          p++;
        }

        if (v == 38u && *p == ';') {
          /*
           * Extended color -- only mode 2 (24-bit truecolor,
           * "38;2;r;g;bm") is understood; util/ascii.c's
           * pm_metal_util_ascii_log_rainbow is the only emitter and never
           * emits anything else. mode + r/g/b are consumed here directly
           * (not left to the loop's own per-number dispatch below) since
           * they are one extended-color unit, not four independent SGR
           * codes.
           */
          uint32_t mode;
          uint32_t rr;
          uint32_t gg;
          uint32_t bb;

          p++;
          mode = 0;
          while (*p >= '0' && *p <= '9') {
            mode = mode * 10u + (uint32_t)(*p - '0');
            p++;
          }

          rr = 0;
          gg = 0;
          bb = 0;
          if (mode == 2u) {
            if (*p == ';') {
              p++;
              while (*p >= '0' && *p <= '9') {
                rr = rr * 10u + (uint32_t)(*p - '0');
                p++;
              }
            }

            if (*p == ';') {
              p++;
              while (*p >= '0' && *p <= '9') {
                gg = gg * 10u + (uint32_t)(*p - '0');
                p++;
              }
            }

            if (*p == ';') {
              p++;
              while (*p >= '0' && *p <= '9') {
                bb = bb * 10u + (uint32_t)(*p - '0');
                p++;
              }
            }

            fg = PM_METAL_GFX_RGB((uint8_t)rr, (uint8_t)gg, (uint8_t)bb);
          }
        } else if (v == 0u) {
          fg = def_fg;
        } else if (v == 2u) {
          fg = COL_LOG_DIM;
        } else if (v == 31u) {
          fg = COL_LOG_FAIL;
        } else if (v == 32u) {
          fg = COL_LOG_OK;
        } else if (v == 33u) {
          fg = COL_LOG_WARN;
        } else if (v == 34u) {
          fg = COL_PROMPT_PATH;
        } else if (v == 35u) {
          fg = COL_REPL_PROMPT;
        } else if (v == 36u) {
          fg = COL_LOG_ACCENT;
        }

        if (*p == ';') {
          p++;
        }
      }

      if (*p == 'm') {
        p++;
      }

      continue;
    }

    ch[0] = *p;
    pm_metal_gfx_draw_text(cx, y, ch, fg, COL_CONSOLE_BG, 0);
    cx += (int32_t)fw;
    cols++;
    p++;
  }
}

int32_t MetalUiStatusGeom(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
  if (gMetalUiStatus == NULL) {
    return -1;
  }

  /* Ensure layout coords match current FB before callers present_rect. */
  MetalUiLayout();
  if (x != NULL) {
    *x = gMetalUiStatus->x;
  }

  if (y != NULL) {
    *y = gMetalUiStatus->y;
  }

  if (w != NULL) {
    *w = gMetalUiStatus->w;
  }

  if (h != NULL) {
    *h = gMetalUiStatus->h;
  }

  return 0;
}

/**
 * The composing line's row count, folded into the console's own scroll
 * region as trailing rows (see MetalUiConsoleTotalRows in widget.c) --
 * 0 when this isn't the one console that ever shows it, or a live guest
 * (fullscreen, or windowed on this tab) currently owns the screen/tab
 * content instead. process.h lives here (paint.c), not widget.c, hence
 * this indirection rather than widget.c reading con->u.console.show_input
 * and the process state directly.
 */
uint32_t MetalUiConsoleLiveInputRows(metal_ui_widget_t *con)
{
  pm_metal_process_id_t pid;

  if (con == NULL || con != gMetalUiSysConsole || con->u.console.show_input == 0) {
    return 0;
  }

  pid = pm_metal_process_current();
  if (pid != PM_METAL_PROCESS_ID_INVALID) {
    pm_metal_process_ui_kind_t kind;

    kind = pm_metal_process_ui_kind(pid);
    if (kind == PM_METAL_PROC_UI_FULLSCREEN) {
      return 0;
    }

    if (kind == PM_METAL_PROC_UI_TAB && pm_metal_process_tab(pid) == pm_metal_ui_tab_active()) {
      return 0;
    }
  }

  return MetalUiInputVisualRows(MetalUiInputCurrentWrap());
}

/**
 * Draw the composing-line row @a vrow (0-based within the live input,
 * i.e. combined_idx - con->u.console.count -- see the call site) at
 * screen row @a ty. vrow==0 also gets the prompt prefix (REPL ">>> "/
 * "... " or the C-shell "host:~$"); wrapped continuation rows don't.
 * Split out of MetalUiPaintConsole's row loop purely to keep that loop's
 * two branches (history row / live row) readable.
 */
static void MetalUiPaintConsoleInputRow(metal_ui_widget_t *con,
                                        uint32_t           vrow,
                                        int32_t            ty,
                                        uint32_t           wrap,
                                        uint32_t           caret_row,
                                        uint32_t           caret_col)
{
  int32_t     cx;
  uint32_t    fw;
  char        rowbuf[CONSOLE_COLS];
  const char *host;
  uintptr_t   host_len;

  fw = pm_metal_gfx_font_width();
  if (fw == 0u) {
    fw = UI_FONT_W;
  }

  cx = con->x + 2;

  if (vrow == 0u) {
    if (pm_metal_py_repl_active()) {
      /* REPL live: ">>> " / "... " — same bold-magenta cue as COM1/scrollback
       * (shell.c's MetalShellPromptAnsi), not the stale C-shell host:~$ this
       * used to hardcode regardless of REPL state. */
      const char *prompt = pm_metal_py_repl_prompt();
      uintptr_t   plen   = strlen(prompt);

      pm_metal_gfx_draw_text(cx, ty, prompt, COL_REPL_PROMPT, COL_CONSOLE_BG, 0);
      cx += (int32_t)(plen * fw);
    } else {
      host = pm_metal_host_name_cstr();
      if (host == NULL || host[0] == '\0') {
        host = "metal";
      }

      host_len = strlen(host);

      /* hostname green, :~ blue, $ green, gap, then buffer row 0 */
      pm_metal_gfx_draw_text(cx, ty, host, COL_LOG_OK, COL_CONSOLE_BG, 0);
      cx += (int32_t)(host_len * fw);
      pm_metal_gfx_draw_text(cx, ty, ":~", COL_PROMPT_PATH, COL_CONSOLE_BG, 0);
      cx += (int32_t)(2u * fw);
      pm_metal_gfx_draw_text(cx, ty, "$", COL_LOG_OK, COL_CONSOLE_BG, 0);
      cx += (int32_t)fw;
      cx += (int32_t)fw;
    }
  }

  (void)MetalUiInputVisualRowText(wrap, vrow, rowbuf, sizeof(rowbuf));
  if (rowbuf[0] != '\0') {
    pm_metal_gfx_draw_text(cx, ty, rowbuf, COL_INPUT_FG, COL_CONSOLE_BG, 0);
  }

  if (con->u.console.cursor_on && caret_row == vrow) {
    char caret[2];

    caret[0] = (char)0xDB;
    caret[1] = '\0';
    pm_metal_gfx_draw_text(
      cx + (int32_t)(caret_col * fw), ty, caret, COL_INPUT_FG, COL_CONSOLE_BG, 0);
  }
}

/**
 * Whole tab is one scrollable console: scrollback history AND the line
 * you're currently typing share this single widget, its one view_off,
 * and its one scrollbar (see MetalUiConsoleTotalRows) -- no separate
 * input strip/divider below it.
 */
static void MetalUiPaintConsole(metal_ui_widget_t *con)
{
  uint32_t fw;
  uint32_t fh;
  uint32_t cols;
  uint32_t rows;
  uint32_t visible;
  uint32_t start;
  uint32_t total;
  uint32_t input_rows;
  uint32_t wrap;
  uint32_t caret_row;
  uint32_t caret_col;
  uint32_t i;
  int32_t  ty;
  int32_t  text_w;
  int32_t  trx;
  int32_t  try;
  int32_t  trw;
  int32_t  trh;
  int32_t  thy;
  int32_t  thh;

  fw = pm_metal_gfx_font_width();
  fh = pm_metal_gfx_font_height();
  if (fw == 0 || fh == 0 || con->w < (int32_t)fw || con->h < (int32_t)fh) {
    return;
  }

  pm_metal_gfx_fill_rect(con->x, con->y, con->w, con->h, COL_CONSOLE_BG);

  text_w = con->w - UI_SCROLL_W;
  if (text_w < (int32_t)fw) {
    text_w = con->w;
  }

  cols = (uint32_t)text_w / fw;
  rows = (uint32_t)con->h / fh;
  if (cols == 0 || rows == 0) {
    return;
  }

  if (cols > CONSOLE_COLS - 1) {
    cols = CONSOLE_COLS - 1;
  }

  MetalUiConsoleClampView(con);

  input_rows = MetalUiConsoleLiveInputRows(con);
  total      = con->u.console.count + input_rows;

  visible = total;
  if (visible > rows) {
    visible = rows;
  }

  if (total <= rows) {
    start = 0;
  } else {
    start = total - rows - con->u.console.view_off;
  }

  wrap      = 0;
  caret_row = 0;
  caret_col = 0;
  if (input_rows > 0u) {
    wrap = MetalUiInputCurrentWrap();
    MetalUiInputCaretCell(wrap, &caret_row, &caret_col);
  }

  ty = con->y;
  for (i = 0; i < visible; i++) {
    uint32_t combined;

    combined = start + i;
    if (combined < con->u.console.count) {
      uint32_t             idx;
      const char          *line;
      char                 buf[CONSOLE_COLS];
      uint32_t             len;
      uint32_t             has_ansi;
      pm_metal_gfx_color_t fg;

      idx = (con->u.console.head + CONSOLE_LINES - con->u.console.count + combined) % CONSOLE_LINES;
      line     = con->u.console.lines[idx];
      len      = 0;
      has_ansi = 0;
      while (line[len] != '\0' && len < CONSOLE_COLS - 1u) {
        if (line[len] == '\033') {
          has_ansi = 1;
        }

        buf[len] = line[len];
        len++;
      }

      buf[len] = '\0';
      switch ((pm_metal_log_style_t)con->u.console.styles[idx]) {
      case PM_METAL_LOG_STYLE_DIM:
        fg = COL_LOG_DIM;
        break;
      case PM_METAL_LOG_STYLE_OK:
        fg = COL_LOG_OK;
        break;
      case PM_METAL_LOG_STYLE_WARN:
        fg = COL_LOG_WARN;
        break;
      case PM_METAL_LOG_STYLE_FAIL:
        fg = COL_LOG_FAIL;
        break;
      case PM_METAL_LOG_STYLE_ACCENT:
        fg = COL_LOG_ACCENT;
        break;
      case PM_METAL_LOG_STYLE_DEFAULT:
      default:
        fg = COL_CONSOLE_FG;
        break;
      }

      if (has_ansi != 0u) {
        MetalUiDrawTextAnsi(con->x + 2, ty, buf, fg, cols);
      } else {
        if (len > cols) {
          buf[cols] = '\0';
        }

        pm_metal_gfx_draw_text(con->x + 2, ty, buf, fg, COL_CONSOLE_BG, 0);
      }
    } else {
      MetalUiPaintConsoleInputRow(
        con, combined - con->u.console.count, ty, wrap, caret_row, caret_col);
    }

    ty += (int32_t)fh;
  }

  if (MetalUiConsoleScrollBarGeom(con, &trx, &try, &trw, &trh, &thy, &thh)) {
    pm_metal_gfx_fill_rect(trx, try, trw, trh, COL_SCROLL_TRACK);
    pm_metal_gfx_fill_rect(trx, try, 1, trh, COL_SCROLL_EDGE);
    pm_metal_gfx_fill_rect(trx + 1, thy, trw - 2, thh, COL_SCROLL_THUMB);
  } else if (text_w < con->w) {
    pm_metal_gfx_fill_rect(
      con->x + con->w - UI_SCROLL_W, con->y, UI_SCROLL_W, con->h, COL_SCROLL_TRACK);
  }
}

void MetalUiPaintSysConsole(void)
{
  if (gMetalUiSysConsole == NULL) {
    return;
  }

  MetalUiPaintConsole(gMetalUiSysConsole);
}

static void MetalUiPaintTabsStrip(metal_ui_widget_t *tabs)
{
  uint32_t i;
  int32_t  x;
  uint32_t fw;

  pm_metal_gfx_fill_rect(tabs->x, tabs->y, tabs->w, tabs->h, COL_TAB);
  fw = pm_metal_gfx_font_width();
  x  = tabs->x + 2;

  for (i = 0; i < tabs->u.tabs.n; i++) {
    metal_ui_widget_t   *tab;
    int32_t              tw;
    uint32_t             tlen;
    pm_metal_gfx_color_t face;

    tab = tabs->u.tabs.tabs[i];
    if (tab == NULL) {
      continue;
    }

    tlen = 0;
    while (tab->title[tlen] != '\0') {
      tlen++;
    }

    tw = (int32_t)((tlen + 2) * fw) + 16;
    if (tw < 64) {
      tw = 64;
    }

    if (x + tw > tabs->x + tabs->w - 4) {
      break;
    }

    if (i == tabs->u.tabs.active) {
      face = COL_TAB_ON;
    } else if ((int32_t)i == tabs->u.tabs.hover) {
      face = COL_TAB_HOVER;
    } else {
      face = COL_TAB_OFF;
    }

    pm_metal_gfx_fill_rect(x, tabs->y + 2, tw, tabs->h - 2, face);
    pm_metal_gfx_bevel_rect(x,
                            tabs->y + 2,
                            tw,
                            tabs->h - 2,
                            (i == tabs->u.tabs.active) ? 1 : 0,
                            COL_BEVEL_HI,
                            COL_BEVEL_LO);
    pm_metal_gfx_draw_text(x + 8, tabs->y + 6, tab->title, COL_TAB_TXT, face, 1);
    x += tw + 4;
  }
}

static uint32_t MetalUiNetIfHealth(const pm_metal_net_ip_ifcfg_t *cfg)
{
  uint32_t ip;
  uint32_t dns;
  int32_t  is_lo;

  if (cfg == NULL) {
    return NET_HEALTH_DOWN;
  }

  /*
   * IPv4 is authoritative (FillIfcfg clears link_up while DHCP is still
   * pending — that must read red/down, not "link up but no DNS").
   */
  if (pm_metal_util_ip4_parse(cfg->ip, &ip) != 0 || pm_metal_util_ip4_is_unspecified(ip)) {
    return NET_HEALTH_DOWN;
  }

  is_lo = (strcmp(cfg->name, "lo") == 0) ? 1 : 0;
  if (is_lo != 0) {
    return NET_HEALTH_GOOD;
  }

  if (cfg->dns[0] == '\0' || pm_metal_util_ip4_parse(cfg->dns, &dns) != 0 ||
      pm_metal_util_ip4_is_unspecified(dns)) {
    return NET_HEALTH_PARTIAL;
  }

  return NET_HEALTH_GOOD;
}

static pm_metal_gfx_color_t MetalUiNetHealthColor(uint32_t health)
{
  if (health == NET_HEALTH_GOOD) {
    return COL_NET_UP;
  }

  if (health == NET_HEALTH_PARTIAL) {
    return COL_LOG_WARN;
  }

  return COL_NET_DOWN;
}

static pm_metal_gfx_color_t MetalUiFpsColor(uint32_t hz)
{
  if (hz == 0u) {
    return COL_LOG_DIM; /* idle — no presents lately */
  }

  if (hz >= 50u) {
    return COL_LOG_OK; /* smooth UI / 60 Hz class */
  }

  if (hz >= 30u) {
    return COL_LOG_ACCENT; /* Doom TICRATE class */
  }

  if (hz >= 20u) {
    return COL_LOG_WARN;
  }

  return COL_LOG_FAIL;
}

static uint32_t MetalUiAudioPacked(void)
{
  uint32_t vol;

  vol = pm_metal_audio_volume_get();
  if (vol > 100u) {
    vol = 100u;
  }

  return ((pm_metal_audio_ready() ? 1u : 0u) << 9) | ((pm_metal_audio_muted() ? 1u : 0u) << 8) |
         vol;
}

static void MetalUiStatusSnapshot(uint32_t *clock_tod,
                                  uint32_t *net_health,
                                  uint32_t *if_count,
                                  uint32_t *ntp_bit,
                                  uint32_t *fps_hz,
                                  uint32_t *keyb,
                                  uint32_t *aud_packed)
{
  uint64_t                ms;
  uint32_t                tod;
  uint32_t                n;
  uint32_t                i;
  uint32_t                health;
  pm_metal_net_ip_ifcfg_t cfg;

  ms          = pm_metal_tz_local_ms();
  tod         = (uint32_t)((ms / 1000ull) % 86400ull);
  *clock_tod  = (tod / 3600u) * 60u + ((tod % 3600u) / 60u);
  *fps_hz     = pm_metal_gfx_fps();
  *keyb       = pm_metal_input_keyb_get();
  *aud_packed = MetalUiAudioPacked();

  n      = pm_metal_net_ip_if_count();
  health = 0;
  if (n > 16u) {
    n = 16u;
  }

  for (i = 0; i < n; i++) {
    uint32_t h;

    if (pm_metal_net_ip_if_get_index(i, &cfg) != 0) {
      h = NET_HEALTH_DOWN;
    } else {
      h = MetalUiNetIfHealth(&cfg);
    }

    health |= (h & 3u) << (i * 2u);
  }

  *net_health = health;
  *if_count   = n;
  *ntp_bit    = (pm_metal_net_ntp_last_unix_ms() != 0ull) ? 1u : 0u;
}

static void MetalUiPaintStatusBar(metal_ui_widget_t *w)
{
  int32_t                 clock_w;
  int32_t                 clock_x;
  int32_t                 fps_w;
  int32_t                 fps_x;
  int32_t                 keyb_w;
  int32_t                 keyb_x;
  int32_t                 aud_w;
  int32_t                 aud_x;
  int32_t                 tray_right;
  int32_t                 tray_w;
  int32_t                 tx;
  int32_t                 ty;
  int32_t                 left_max;
  int32_t                 fill_w;
  uint32_t                n;
  uint32_t                i;
  uint32_t                fps_hz;
  uint32_t                vol;
  uint64_t                ms;
  uint32_t                tod;
  uint32_t                hour;
  uint32_t                min;
  char                    clock[8];
  char                    fps[8];
  char                    keyb[4];
  const char             *keyb_name;
  const char             *mute_lbl;
  char                    left[STATUS_CHARS];
  uintptr_t               left_n;
  uintptr_t               fps_chars;
  uintptr_t               keyb_chars;
  uintptr_t               max_chars;
  pm_metal_net_ip_ifcfg_t cfg;
  pm_metal_gfx_color_t    fps_fg;
  pm_metal_gfx_color_t    rdy_fg;

  pm_metal_gfx_fill_rect(w->x, w->y, w->w, w->h, COL_STATUS);
  pm_metal_gfx_bevel_rect(w->x, w->y, w->w, w->h, 0, COL_BEVEL_HI, COL_BEVEL_LO);

  clock_w = 8 + UI_CLOCK_CHARS * UI_FONT_W + 8;
  fps_w   = 8 + UI_FPS_CHARS * UI_FONT_W + 8;
  keyb_w  = 8 + UI_KEYB_CHARS * UI_FONT_W + 8;
  /* ready bullet + mute letter + slider */
  aud_w = 8 + 6 + 4 + UI_FONT_W + 4 + UI_AUD_SLIDER_W + 8;
  if (clock_w + fps_w + keyb_w + aud_w + 40 > w->w) {
    fps_w = 8 + 3 * UI_FONT_W + 8; /* fall back to "99" */
  }

  clock_x = w->x + w->w - 4 - clock_w;
  fps_x   = clock_x - 6 - fps_w;
  if (fps_x < w->x + 4) {
    fps_x = w->x + 4;
  }

  if (clock_x < fps_x + fps_w + 6) {
    clock_x = fps_x + fps_w + 6;
  }

  /* Keyb layout cell (2-letter, e.g. "us"/"de") left of the FPS cell —
   * same chrome style as fps/clock, cycled by Ctrl+Alt+Home (shell.c). */
  keyb_x = fps_x - 6 - keyb_w;
  if (keyb_x < w->x + 4) {
    keyb_x = w->x + 4;
  }

  if (fps_x < keyb_x + keyb_w + 6) {
    fps_x = keyb_x + keyb_w + 6;
  }

  aud_x = keyb_x - 6 - aud_w;
  if (aud_x < w->x + 4) {
    aud_x = w->x + 4;
    aud_w = keyb_x - 6 - aud_x;
    if (aud_w < 40) {
      aud_w = 0;
    }
  }

  /* Systray width: colored bullet + name + pad per iface. */
  n      = pm_metal_net_ip_if_count();
  tray_w = 0;
  for (i = 0; i < n; i++) {
    if (pm_metal_net_ip_if_get_index(i, &cfg) != 0) {
      continue;
    }

    tray_w += 10 + (int32_t)strlen(cfg.name) * UI_FONT_W + 6;
  }

  tray_right = (aud_w > 0) ? (aud_x - 8) : (keyb_x - 8);
  if (tray_w > 0 && tray_right - tray_w < w->x + 8) {
    tray_w = tray_right - (w->x + 8);
    if (tray_w < 0) {
      tray_w = 0;
    }
  }

  left_max = (tray_w > 0) ? (tray_right - tray_w - 8) : tray_right;
  if (left_max < w->x + 8) {
    left_max = w->x + 8;
  }

  max_chars = (uintptr_t)((left_max - (w->x + 8)) / UI_FONT_W);
  left_n    = strlen(w->u.status.text);
  if (left_n > max_chars) {
    left_n = max_chars;
  }

  if (left_n >= sizeof(left)) {
    left_n = sizeof(left) - 1u;
  }

  memcpy(left, w->u.status.text, left_n);
  left[left_n] = '\0';
  if (left_n > 0) {
    pm_metal_gfx_draw_text(w->x + 8, w->y + 4, left, COL_STATUS_TXT, COL_STATUS, 1);
  }

  /* Net systray left of the audio/keyb cells. */
  tx = tray_right - tray_w;
  ty = w->y + 4;
  if (tx < w->x + 8) {
    tx = w->x + 8;
  }

  for (i = 0; i < n; i++) {
    int32_t              item_w;
    pm_metal_gfx_color_t fg;
    uintptr_t            namelen;
    uint32_t             health;

    if (pm_metal_net_ip_if_get_index(i, &cfg) != 0) {
      continue;
    }

    namelen = strlen(cfg.name);
    item_w  = 10 + (int32_t)namelen * UI_FONT_W + 6;
    if (tx + item_w > tray_right) {
      break;
    }

    health = MetalUiNetIfHealth(&cfg);
    fg     = MetalUiNetHealthColor(health);
    pm_metal_gfx_fill_rect(tx + 2, w->y + 9, 6, 6, fg);
    pm_metal_gfx_draw_text(tx + 10, ty, cfg.name, fg, COL_STATUS, 1);
    tx += item_w;
  }

  mAudValid = 0;
  if (aud_w > 0) {
    /* Separator tray | audio. */
    if (tray_w > 0) {
      pm_metal_gfx_fill_rect(aud_x - 5, w->y + 5, 1, w->h - 10, COL_BEVEL_LO);
      pm_metal_gfx_fill_rect(aud_x - 4, w->y + 5, 1, w->h - 10, COL_BEVEL_HI);
    }

    pm_metal_gfx_fill_rect(aud_x, w->y + 2, aud_w, w->h - 4, COL_STATUS_CLK);
    pm_metal_gfx_bevel_rect(aud_x, w->y + 2, aud_w, w->h - 4, 0, COL_BEVEL_LO, COL_BEVEL_HI);

    rdy_fg = pm_metal_audio_ready() ? COL_LOG_OK : COL_LOG_FAIL;
    pm_metal_gfx_fill_rect(aud_x + 4, w->y + 9, 6, 6, rdy_fg);

    mute_lbl  = pm_metal_audio_muted() ? "M" : "A";
    mAudMuteX = aud_x + 4 + 6 + 4;
    mAudMuteW = UI_FONT_W + 4;
    pm_metal_gfx_draw_text(mAudMuteX,
                           w->y + 4,
                           mute_lbl,
                           pm_metal_audio_muted() ? COL_LOG_WARN : COL_STATUS_TXT,
                           COL_STATUS_CLK,
                           1);

    mAudSlideX = mAudMuteX + mAudMuteW;
    mAudSlideW = UI_AUD_SLIDER_W;
    if (mAudSlideX + mAudSlideW > aud_x + aud_w - 4) {
      mAudSlideW = aud_x + aud_w - 4 - mAudSlideX;
    }

    if (mAudSlideW > 4) {
      vol = pm_metal_audio_volume_get();
      if (vol > 100u) {
        vol = 100u;
      }

      if (pm_metal_audio_muted()) {
        vol = 0u;
      }

      pm_metal_gfx_fill_rect(mAudSlideX, w->y + 10, mAudSlideW, 4, COL_BEVEL_LO);
      fill_w = (int32_t)((vol * (uint32_t)mAudSlideW) / 100u);
      if (fill_w > 0) {
        pm_metal_gfx_fill_rect(mAudSlideX, w->y + 10, fill_w, 4, COL_LOG_OK);
      }
    }

    mAudY     = w->y;
    mAudH     = w->h;
    mAudValid = 1;

    /* Separator audio | keyb. */
    pm_metal_gfx_fill_rect(keyb_x - 5, w->y + 5, 1, w->h - 10, COL_BEVEL_LO);
    pm_metal_gfx_fill_rect(keyb_x - 4, w->y + 5, 1, w->h - 10, COL_BEVEL_HI);
  } else if (tray_w > 0) {
    /* Separator tray | keyb (no audio cell). */
    pm_metal_gfx_fill_rect(keyb_x - 5, w->y + 5, 1, w->h - 10, COL_BEVEL_LO);
    pm_metal_gfx_fill_rect(keyb_x - 4, w->y + 5, 1, w->h - 10, COL_BEVEL_HI);
  }

  keyb_name = pm_metal_input_keyb_name(pm_metal_input_keyb_get());
  snprintf(keyb, sizeof(keyb), "%s", (keyb_name != NULL) ? keyb_name : "??");
  keyb_chars = strlen(keyb);

  pm_metal_gfx_fill_rect(keyb_x, w->y + 2, keyb_w, w->h - 4, COL_STATUS_CLK);
  pm_metal_gfx_bevel_rect(keyb_x, w->y + 2, keyb_w, w->h - 4, 0, COL_BEVEL_LO, COL_BEVEL_HI);
  pm_metal_gfx_draw_text(keyb_x + (keyb_w - (int32_t)keyb_chars * UI_FONT_W) / 2,
                         w->y + 4,
                         keyb,
                         COL_STATUS_TXT,
                         COL_STATUS_CLK,
                         1);

  /* Separator keyb | fps. */
  pm_metal_gfx_fill_rect(fps_x - 5, w->y + 5, 1, w->h - 10, COL_BEVEL_LO);
  pm_metal_gfx_fill_rect(fps_x - 4, w->y + 5, 1, w->h - 10, COL_BEVEL_HI);

  fps_hz = pm_metal_gfx_fps();
  fps_fg = MetalUiFpsColor(fps_hz);
  if (fps_hz == 0u) {
    snprintf(fps, sizeof(fps), "%s", "-fps");
  } else {
    if (fps_hz > 999u) {
      fps_hz = 999u;
    }

    snprintf(fps, sizeof(fps), "%ufps", fps_hz);
  }

  fps_chars = strlen(fps);
  if (fps_chars > UI_FPS_CHARS) {
    snprintf(fps, sizeof(fps), "%u", fps_hz);
    fps_chars = strlen(fps);
  }

  pm_metal_gfx_fill_rect(fps_x, w->y + 2, fps_w, w->h - 4, COL_STATUS_CLK);
  pm_metal_gfx_bevel_rect(fps_x, w->y + 2, fps_w, w->h - 4, 0, COL_BEVEL_LO, COL_BEVEL_HI);
  pm_metal_gfx_draw_text(
    fps_x + (fps_w - (int32_t)fps_chars * UI_FONT_W) / 2, w->y + 4, fps, fps_fg, COL_STATUS_CLK, 1);

  /* Separator fps | clock. */
  pm_metal_gfx_fill_rect(clock_x - 5, w->y + 5, 1, w->h - 10, COL_BEVEL_LO);
  pm_metal_gfx_fill_rect(clock_x - 4, w->y + 5, 1, w->h - 10, COL_BEVEL_HI);

  /* Separated clock field (inset). */
  pm_metal_gfx_fill_rect(clock_x, w->y + 2, clock_w, w->h - 4, COL_STATUS_CLK);
  pm_metal_gfx_bevel_rect(clock_x, w->y + 2, clock_w, w->h - 4, 0, COL_BEVEL_LO, COL_BEVEL_HI);

  ms   = pm_metal_tz_local_ms();
  tod  = (uint32_t)((ms / 1000ull) % 86400ull);
  hour = tod / 3600u;
  min  = (tod % 3600u) / 60u;
  snprintf(clock, sizeof(clock), "%02u:%02u", hour, min);
  pm_metal_gfx_draw_text(clock_x + (clock_w - UI_CLOCK_CHARS * UI_FONT_W) / 2,
                         w->y + 4,
                         clock,
                         (pm_metal_net_ntp_last_unix_ms() != 0ull) ? COL_STATUS_TXT : COL_LOG_WARN,
                         COL_STATUS_CLK,
                         1);

  MetalUiStatusSnapshot(&mStatusClockTod,
                        &mStatusNetHealth,
                        &mStatusIfCount,
                        &mStatusNtpBit,
                        &mStatusFpsHz,
                        &mStatusKeyb,
                        &mStatusAudPacked);
}

void MetalUiPaintStatusBarOnly(void)
{
  pm_metal_gfx_surface_h prev;

  if (gMetalUiStatus == NULL) {
    return;
  }

  prev = pm_metal_gfx_draw_surface();
  pm_metal_gfx_set_surface(PM_METAL_GFX_SURFACE_DEFAULT);
  MetalUiLayout();
  MetalUiPaintStatusBar(gMetalUiStatus);
  pm_metal_gfx_set_surface(prev);
}

static void MetalUiPaintWidget(metal_ui_widget_t *w)
{
  metal_ui_widget_t *c;

  if (w == NULL) {
    return;
  }

  switch (w->kind) {
  case METAL_UI_KIND_WINDOW:
    pm_metal_gfx_fill_rect(w->x, w->y, w->w, w->h, COL_WINDOW);
    pm_metal_gfx_bevel_rect(w->x, w->y, w->w, w->h, 1, COL_BEVEL_HI, COL_BEVEL_LO);
    pm_metal_gfx_fill_rect(w->x + 4, w->y + 4, w->w - 8, UI_TITLE_H - 4, COL_TITLE);
    pm_metal_gfx_draw_text(w->x + 12, w->y + 8, w->title, COL_TITLE_TXT, COL_TITLE, 1);
    break;

  case METAL_UI_KIND_TABS:
    MetalUiPaintTabsStrip(w);
    if (w->u.tabs.n > 0 && w->u.tabs.active < w->u.tabs.n) {
      MetalUiPaintWidget(w->u.tabs.tabs[w->u.tabs.active]);
    }

    return;

  case METAL_UI_KIND_FRAME:
    if (!MetalUiTabGuestOwnsContent(w)) {
      pm_metal_gfx_fill_rect(w->x, w->y, w->w, w->h, COL_FRAME_FACE);
    }

    pm_metal_gfx_bevel_rect(w->x, w->y, w->w, w->h, 0, COL_BEVEL_HI, COL_BEVEL_LO);
    break;

  case METAL_UI_KIND_CONSOLE:
    if (MetalUiTabGuestOwnsContent(w)) {
      /* Windowed guest owns the whole content — no prompt under the game. */
      return;
    }

    MetalUiPaintConsole(w);
    return;

  case METAL_UI_KIND_STATUS_BAR:
    MetalUiPaintStatusBar(w);
    return;

  default:
    break;
  }

  for (c = w->child; c != NULL; c = c->next) {
    if (w->kind == METAL_UI_KIND_WINDOW && c->kind == METAL_UI_KIND_TABS) {
      MetalUiPaintWidget(c);
    } else if (w->kind == METAL_UI_KIND_WINDOW && c->kind == METAL_UI_KIND_STATUS_BAR) {
      MetalUiPaintWidget(c);
    } else if (w->kind != METAL_UI_KIND_WINDOW) {
      MetalUiPaintWidget(c);
    }
  }
}

void MetalUiLayout(void)
{
  pm_metal_gfx_surface_t *surf;

  surf = pm_metal_gfx_surface();
  if (gMetalUiShellRoot == NULL || surf == NULL) {
    return;
  }

  MetalUiLayoutWindow(gMetalUiShellRoot, (int32_t)surf->width, (int32_t)surf->height);
}

void MetalUiPaint(void)
{
  if (gMetalUiShellRoot == NULL) {
    return;
  }

  /*
   * Full clear wipes windowed guest content. When a tab session owns a
   * surface, skip the desktop clear — chrome widgets redraw themselves.
   */
  if (!(pm_metal_process_active() && pm_metal_wasm_stdout_tab() != PM_METAL_UI_HANDLE_INVALID &&
        pm_metal_ui_tab_surface(pm_metal_wasm_stdout_tab()) != PM_METAL_GFX_SURFACE_INVALID)) {
    pm_metal_gfx_clear(COL_DESKTOP);
  }

  MetalUiPaintWidget(gMetalUiShellRoot);
}

int32_t MetalUiStatusNeedsRefresh(void)
{
  uint32_t clock_tod;
  uint32_t net_health;
  uint32_t if_count;
  uint32_t ntp_bit;
  uint32_t fps_hz;
  uint32_t keyb;
  uint32_t aud;

  MetalUiStatusSnapshot(&clock_tod, &net_health, &if_count, &ntp_bit, &fps_hz, &keyb, &aud);
  if (clock_tod != mStatusClockTod || net_health != mStatusNetHealth ||
      if_count != mStatusIfCount || ntp_bit != mStatusNtpBit || fps_hz != mStatusFpsHz ||
      keyb != mStatusKeyb || aud != mStatusAudPacked) {
    return 1;
  }

  return 0;
}

int32_t MetalUiStatusAudioPointer(int32_t x, int32_t y, uint32_t buttons)
{
  static uint32_t prev_buttons;
  int32_t         edge;
  int32_t         held;
  int32_t         handled;
  uint32_t        vol;

  edge         = ((buttons & 1u) != 0u && (prev_buttons & 1u) == 0u) ? 1 : 0;
  held         = ((buttons & 1u) != 0u) ? 1 : 0;
  handled      = 0;
  prev_buttons = buttons;

  if (!mAudValid || y < mAudY || y >= mAudY + mAudH) {
    return 0;
  }

  if (edge && x >= mAudMuteX && x < mAudMuteX + mAudMuteW) {
    pm_metal_audio_mute(pm_metal_audio_muted() ? 0 : 1);
    return 1;
  }

  if ((edge || held) && mAudSlideW > 0 && x >= mAudSlideX && x < mAudSlideX + mAudSlideW) {
    vol = (uint32_t)(((x - mAudSlideX) * 100) / mAudSlideW);
    if (vol > 100u) {
      vol = 100u;
    }

    if (pm_metal_audio_muted() && vol > 0u) {
      pm_metal_audio_mute(0);
    }

    pm_metal_audio_volume_set(vol);
    handled = 1;
  }

  return handled;
}

int pm_metal_ui_status_audio_pointer(int32_t x, int32_t y, uint32_t buttons)
{
  return MetalUiStatusAudioPointer(x, y, buttons);
}
