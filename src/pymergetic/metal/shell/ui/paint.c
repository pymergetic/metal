/** @file
  UI layout + paint (window chrome, console, status tray/clock).
**/
#include "priv.h"

#include <pymergetic/metal/dev/net/net_cfg.h>
#include <pymergetic/metal/dev/net/ntp.h>
#include <pymergetic/metal/dev/random/random.h>
#include <pymergetic/metal/guest/wasm/wasm.h>
#include <pymergetic/metal/guest/process/process.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/host/host.h>
#include <pymergetic/metal/util/ip.h>

#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

STATIC UINT32  mStatusClockTod  = 0xffffffffu;
STATIC UINT32  mStatusNetHealth = 0xffffffffu;
STATIC UINT32  mStatusIfCount   = 0xffffffffu;
STATIC UINT32  mStatusNtpBit    = 0xffffffffu;

/* Per-iface tray color: 0=down, 1=partial (IP no DNS), 2=good — packed 2 bits. */
#define NET_HEALTH_DOWN     0u
#define NET_HEALTH_PARTIAL  1u
#define NET_HEALTH_GOOD     2u

/**
 * True when a live wasm session draws into this tab's surface (windowed
 * guest). Chrome must not fill that content rect or the game vanishes.
 * Fullscreen (`run`) draws DEFAULT — not a tab content owner (and must not
 * keep painting the shared input strip over the game).
 */
STATIC
INT32
MetalUiTabGuestOwnsContent (
  metal_ui_widget_t  *w
  )
{
  metal_ui_widget_t       *tab;
  pm_metal_process_id_t    pid;

  tab = w;
  while (tab != NULL && tab->kind != METAL_UI_KIND_TAB) {
    tab = tab->parent;
  }

  if (tab == NULL || tab->surface == PM_METAL_GFX_SURFACE_INVALID) {
    return 0;
  }

  if (!pm_metal_wasm_session_active ()) {
    return 0;
  }

  pid = pm_metal_process_current ();
  if (pid != PM_METAL_PROCESS_ID_INVALID
      && pm_metal_process_ui_kind (pid) == PM_METAL_PROC_UI_FULLSCREEN)
  {
    return 0;
  }

  return pm_metal_wasm_stdout_tab () == tab->handle;
}

STATIC
VOID
MetalUiLayoutWindow (
  metal_ui_widget_t  *win,
  INT32               sw,
  INT32               sh
  )
{
  metal_ui_widget_t  *tabs;
  metal_ui_widget_t  *st;
  metal_ui_widget_t  *tab;
  metal_ui_widget_t  *frame;
  metal_ui_widget_t  *con;
  UINT32              i;
  INT32               x;
  INT32               y;
  INT32               w;
  INT32               h;
  INT32               body_y;
  INT32               body_h;
  INT32               input_h;
  UINT32              fh;

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
  fh     = pm_metal_gfx_font_height ();

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
     * Shell input is shared on console/idle tabs. A live windowed guest
     * owns the whole frame — no prompt row (doom fills the tab content).
     */
    input_h = 0;
    if (!MetalUiTabGuestOwnsContent (tab)) {
      input_h = (INT32)fh * UI_INPUT_ROWS + 4;
    }

    con->x = frame->x + UI_FRAME_PAD;
    con->y = frame->y + UI_FRAME_PAD;
    con->w = frame->w - 2 * UI_FRAME_PAD;
    con->h = frame->h - 2 * UI_FRAME_PAD - input_h;

    if (tab->surface != PM_METAL_GFX_SURFACE_INVALID) {
      /* Guest = padded frame interior (no prompt strip while playing). */
      pm_metal_gfx_surface_set_rect (
        tab->surface,
        con->x,
        con->y,
        con->w,
        con->h
        );
    }
  }
}

/**
 * Draw text with a minimal CSI SGR subset (same codes log/prompt emit).
 * Advances at most max_cols glyph cells; escape bytes do not consume cells.
 */
STATIC
VOID
MetalUiDrawTextAnsi (
  INT32                 x,
  INT32                 y,
  CONST CHAR8          *text,
  pm_metal_gfx_color_t  def_fg,
  UINT32                max_cols
  )
{
  CONST CHAR8          *p;
  INT32                 cx;
  UINT32                cols;
  UINT32                fw;
  pm_metal_gfx_color_t  fg;
  CHAR8                 ch[2];

  if (text == NULL || max_cols == 0u) {
    return;
  }

  fw = pm_metal_gfx_font_width ();
  if (fw == 0u) {
    fw = UI_FONT_W;
  }

  p    = text;
  cx   = x;
  cols = 0;
  fg   = def_fg;
  ch[1] = '\0';

  while (*p != '\0' && cols < max_cols) {
    if (p[0] == '\033' && p[1] == '[') {
      p += 2;
      while (*p != '\0' && *p != 'm') {
        UINT32  v;

        if (*p < '0' || *p > '9') {
          p++;
          continue;
        }

        v = 0;
        while (*p >= '0' && *p <= '9') {
          v = v * 10u + (UINT32)(*p - '0');
          p++;
        }

        if (v == 0u) {
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
    pm_metal_gfx_draw_text (cx, y, ch, fg, COL_CONSOLE_BG, 0);
    cx += (INT32)fw;
    cols++;
    p++;
  }
}

INT32
MetalUiShellInputGeom (
  INT32  *x,
  INT32  *y,
  INT32  *w,
  INT32  *h
  )
{
  metal_ui_widget_t  *con;
  UINT32              fh;

  con = MetalUiActiveConsole ();
  if (con == NULL || gMetalUiSysConsole == NULL) {
    return -1;
  }

  fh = pm_metal_gfx_font_height ();
  if (fh == 0) {
    return -1;
  }

  if (x != NULL) {
    *x = con->x;
  }

  if (y != NULL) {
    *y = con->y + con->h + 2;
  }

  if (w != NULL) {
    *w = con->w;
  }

  if (h != NULL) {
    *h = (INT32)fh + 2;
  }

  return 0;
}

VOID
MetalUiPaintShellInputLine (
  VOID
  )
{
  INT32                    x;
  INT32                    y;
  INT32                    w;
  INT32                    h;
  INT32                    cx;
  UINT32                   fw;
  CONST CHAR8             *host;
  UINTN                    host_len;
  CHAR8                    typed[INPUT_CHARS + 2];
  UINT32                   n;
  pm_metal_process_id_t    pid;

  /* Live guest (fullscreen or windowed on this tab) — no shell prompt. */
  pid = pm_metal_process_current ();
  if (pid != PM_METAL_PROCESS_ID_INVALID) {
    pm_metal_process_ui_kind_t  kind;

    kind = pm_metal_process_ui_kind (pid);
    if (kind == PM_METAL_PROC_UI_FULLSCREEN) {
      return;
    }

    if (kind == PM_METAL_PROC_UI_TAB
        && pm_metal_process_tab (pid) == pm_metal_ui_tab_active ())
    {
      return;
    }
  }

  if (MetalUiShellInputGeom (&x, &y, &w, &h) != 0) {
    return;
  }

  pm_metal_gfx_set_surface (PM_METAL_GFX_SURFACE_DEFAULT);
  pm_metal_gfx_fill_rect (x, y, w, h, COL_CONSOLE_BG);

  host = pm_metal_host_name_cstr ();
  if (host == NULL || host[0] == '\0') {
    host = "metal";
  }

  host_len = AsciiStrLen (host);
  fw       = pm_metal_gfx_font_width ();
  if (fw == 0u) {
    fw = UI_FONT_W;
  }

  cx = x + 2;
  /* hostname green, :~ blue (ANSI 34), $ green, then a real gap before input */
  pm_metal_gfx_draw_text (cx, y, host, COL_LOG_OK, COL_CONSOLE_BG, 0);
  cx += (INT32)(host_len * fw);
  pm_metal_gfx_draw_text (cx, y, ":~", COL_PROMPT_PATH, COL_CONSOLE_BG, 0);
  cx += (INT32)(2u * fw);
  pm_metal_gfx_draw_text (cx, y, "$", COL_LOG_OK, COL_CONSOLE_BG, 0);
  cx += (INT32)fw;
  /* blank cell = space between prompt and typed text / cursor */
  cx += (INT32)fw;

  n = gMetalUiSysConsole->u.console.input_len;
  if (n >= sizeof (typed)) {
    n = (UINT32)(sizeof (typed) - 1u);
  }

  if (n > 0u) {
    CopyMem (typed, gMetalUiSysConsole->u.console.input, n);
  }

  if (gMetalUiSysConsole->u.console.cursor_on && n + 1u < sizeof (typed)) {
    typed[n++] = 0xDB;
  }

  typed[n] = '\0';
  if (n > 0u) {
    pm_metal_gfx_draw_text (cx, y, typed, COL_INPUT_FG, COL_CONSOLE_BG, 0);
  }
}

STATIC
VOID
MetalUiPaintConsole (
  metal_ui_widget_t  *con
  )
{
  UINT32  fw;
  UINT32  fh;
  UINT32  cols;
  UINT32  rows;
  UINT32  visible;
  UINT32  start;
  UINT32  i;
  INT32   ty;
  INT32   text_w;
  INT32   trx;
  INT32   try;
  INT32   trw;
  INT32   trh;
  INT32   thy;
  INT32   thh;

  fw = pm_metal_gfx_font_width ();
  fh = pm_metal_gfx_font_height ();
  if (fw == 0 || fh == 0 || con->w < (INT32)fw || con->h < (INT32)fh) {
    return;
  }

  pm_metal_gfx_fill_rect (con->x, con->y, con->w, con->h, COL_CONSOLE_BG);

  text_w = con->w - UI_SCROLL_W;
  if (text_w < (INT32)fw) {
    text_w = con->w;
  }

  cols = (UINT32)text_w / fw;
  rows = (UINT32)con->h / fh;
  if (cols == 0 || rows == 0) {
    return;
  }

  if (cols > CONSOLE_COLS - 1) {
    cols = CONSOLE_COLS - 1;
  }

  MetalUiConsoleClampView (con);

  visible = con->u.console.count;
  if (visible > rows) {
    visible = rows;
  }

  if (con->u.console.count <= rows) {
    start = 0;
  } else {
    start = con->u.console.count - rows - con->u.console.view_off;
  }

  ty = con->y;
  for (i = 0; i < visible; i++) {
    UINT32                idx;
    CONST CHAR8          *line;
    CHAR8                 buf[CONSOLE_COLS];
    UINT32                len;
    UINT32                has_ansi;
    pm_metal_gfx_color_t  fg;

    idx = (con->u.console.head + CONSOLE_LINES - con->u.console.count + start + i)
          % CONSOLE_LINES;
    line = con->u.console.lines[idx];
    len  = 0;
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
      MetalUiDrawTextAnsi (con->x + 2, ty, buf, fg, cols);
    } else {
      if (len > cols) {
        buf[cols] = '\0';
      }

      pm_metal_gfx_draw_text (con->x + 2, ty, buf, fg, COL_CONSOLE_BG, 0);
    }

    ty += (INT32)fh;
  }

  if (MetalUiConsoleScrollBarGeom (con, &trx, &try, &trw, &trh, &thy, &thh)) {
    pm_metal_gfx_fill_rect (trx, try, trw, trh, COL_SCROLL_TRACK);
    pm_metal_gfx_fill_rect (trx, try, 1, trh, COL_SCROLL_EDGE);
    pm_metal_gfx_fill_rect (trx + 1, thy, trw - 2, thh, COL_SCROLL_THUMB);
  } else if (text_w < con->w) {
    pm_metal_gfx_fill_rect (
      con->x + con->w - UI_SCROLL_W,
      con->y,
      UI_SCROLL_W,
      con->h,
      COL_SCROLL_TRACK
      );
  }

  if (con == MetalUiActiveConsole ()) {
    MetalUiPaintShellInputLine ();
  }
}

STATIC
VOID
MetalUiPaintTabsStrip (
  metal_ui_widget_t  *tabs
  )
{
  UINT32  i;
  INT32   x;
  UINT32  fw;

  pm_metal_gfx_fill_rect (tabs->x, tabs->y, tabs->w, tabs->h, COL_TAB);
  fw = pm_metal_gfx_font_width ();
  x  = tabs->x + 2;

  for (i = 0; i < tabs->u.tabs.n; i++) {
    metal_ui_widget_t  *tab;
    INT32               tw;
    UINT32              tlen;
    pm_metal_gfx_color_t  face;

    tab = tabs->u.tabs.tabs[i];
    if (tab == NULL) {
      continue;
    }

    tlen = 0;
    while (tab->title[tlen] != '\0') {
      tlen++;
    }

    tw = (INT32)((tlen + 2) * fw) + 16;
    if (tw < 64) {
      tw = 64;
    }

    if (x + tw > tabs->x + tabs->w - 4) {
      break;
    }

    if (i == tabs->u.tabs.active) {
      face = COL_TAB_ON;
    } else if ((INT32)i == tabs->u.tabs.hover) {
      face = COL_TAB_HOVER;
    } else {
      face = COL_TAB_OFF;
    }

    pm_metal_gfx_fill_rect (x, tabs->y + 2, tw, tabs->h - 2, face);
    pm_metal_gfx_bevel_rect (
      x,
      tabs->y + 2,
      tw,
      tabs->h - 2,
      (i == tabs->u.tabs.active) ? 1 : 0,
      COL_BEVEL_HI,
      COL_BEVEL_LO
      );
    pm_metal_gfx_draw_text (
      x + 8,
      tabs->y + 6,
      tab->title,
      COL_TAB_TXT,
      face,
      1
      );
    x += tw + 4;
  }
}

STATIC
UINT32
MetalUiNetIfHealth (
  CONST pm_metal_net_ifcfg_t  *cfg
  )
{
  UINT32  ip;
  UINT32  dns;
  INT32   is_lo;

  if (cfg == NULL) {
    return NET_HEALTH_DOWN;
  }

  /*
   * IPv4 is authoritative (FillIfcfg clears link_up while DHCP is still
   * pending — that must read red/down, not "link up but no DNS").
   */
  if (pm_metal_util_ip4_parse (cfg->ip, &ip) != 0
      || pm_metal_util_ip4_is_unspecified (ip))
  {
    return NET_HEALTH_DOWN;
  }

  is_lo = (AsciiStrCmp (cfg->name, "lo") == 0) ? 1 : 0;
  if (is_lo != 0) {
    return NET_HEALTH_GOOD;
  }

  if (cfg->dns[0] == '\0'
      || pm_metal_util_ip4_parse (cfg->dns, &dns) != 0
      || pm_metal_util_ip4_is_unspecified (dns))
  {
    return NET_HEALTH_PARTIAL;
  }

  return NET_HEALTH_GOOD;
}

STATIC
pm_metal_gfx_color_t
MetalUiNetHealthColor (
  UINT32  health
  )
{
  if (health == NET_HEALTH_GOOD) {
    return COL_NET_UP;
  }

  if (health == NET_HEALTH_PARTIAL) {
    return COL_LOG_WARN;
  }

  return COL_NET_DOWN;
}

STATIC
VOID
MetalUiStatusSnapshot (
  UINT32  *clock_tod,
  UINT32  *net_health,
  UINT32  *if_count,
  UINT32  *ntp_bit
  )
{
  UINT64               ms;
  UINT32               tod;
  UINT32               n;
  UINT32               i;
  UINT32               health;
  pm_metal_net_ifcfg_t cfg;

  ms  = pm_metal_tz_local_ms ();
  tod = (UINT32)((ms / 1000ull) % 86400ull);
  *clock_tod = (tod / 3600u) * 60u + ((tod % 3600u) / 60u);

  n      = pm_metal_net_if_count ();
  health = 0;
  if (n > 16u) {
    n = 16u;
  }

  for (i = 0; i < n; i++) {
    UINT32  h;

    if (pm_metal_net_if_get_index (i, &cfg) != 0) {
      h = NET_HEALTH_DOWN;
    } else {
      h = MetalUiNetIfHealth (&cfg);
    }

    health |= (h & 3u) << (i * 2u);
  }

  *net_health = health;
  *if_count   = n;
  *ntp_bit    = (pm_metal_net_ntp_last_unix_ms () != 0ull) ? 1u : 0u;
}

STATIC
VOID
MetalUiPaintStatusBar (
  metal_ui_widget_t  *w
  )
{
  INT32                clock_w;
  INT32                clock_x;
  INT32                tray_right;
  INT32                tray_w;
  INT32                tx;
  INT32                ty;
  INT32                left_max;
  UINT32               n;
  UINT32               i;
  UINT64               ms;
  UINT32               tod;
  UINT32               hour;
  UINT32               min;
  CHAR8               clock[8];
  CHAR8               left[STATUS_CHARS];
  UINTN                left_n;
  UINTN                max_chars;
  pm_metal_net_ifcfg_t cfg;

  pm_metal_gfx_fill_rect (w->x, w->y, w->w, w->h, COL_STATUS);
  pm_metal_gfx_bevel_rect (w->x, w->y, w->w, w->h, 0, COL_BEVEL_HI, COL_BEVEL_LO);

  clock_w = 8 + UI_CLOCK_CHARS * UI_FONT_W + 8;
  if (clock_w + 16 > w->w) {
    clock_w = w->w - 16;
  }

  clock_x = w->x + w->w - 4 - clock_w;
  if (clock_x < w->x + 4) {
    clock_x = w->x + 4;
  }

  /* Systray width: colored bullet + name + pad per iface. */
  n      = pm_metal_net_if_count ();
  tray_w = 0;
  for (i = 0; i < n; i++) {
    if (pm_metal_net_if_get_index (i, &cfg) != 0) {
      continue;
    }

    tray_w += 10 + (INT32)AsciiStrLen (cfg.name) * UI_FONT_W + 6;
  }

  tray_right = clock_x - 8;
  if (tray_w > 0 && tray_right - tray_w < w->x + 8) {
    tray_w = tray_right - (w->x + 8);
    if (tray_w < 0) {
      tray_w = 0;
    }
  }

  left_max = (tray_w > 0) ? (tray_right - tray_w - 8) : (clock_x - 8);
  if (left_max < w->x + 8) {
    left_max = w->x + 8;
  }

  max_chars = (UINTN)((left_max - (w->x + 8)) / UI_FONT_W);
  left_n    = AsciiStrLen (w->u.status.text);
  if (left_n > max_chars) {
    left_n = max_chars;
  }

  if (left_n >= sizeof (left)) {
    left_n = sizeof (left) - 1u;
  }

  CopyMem (left, w->u.status.text, left_n);
  left[left_n] = '\0';
  if (left_n > 0) {
    pm_metal_gfx_draw_text (
      w->x + 8,
      w->y + 4,
      left,
      COL_STATUS_TXT,
      COL_STATUS,
      1
      );
  }

  /* Net systray left of the clock cell. */
  tx = tray_right - tray_w;
  ty = w->y + 4;
  if (tx < w->x + 8) {
    tx = w->x + 8;
  }

  for (i = 0; i < n; i++) {
    INT32                 item_w;
    pm_metal_gfx_color_t  fg;
    UINTN                 namelen;
    UINT32                health;

    if (pm_metal_net_if_get_index (i, &cfg) != 0) {
      continue;
    }

    namelen = AsciiStrLen (cfg.name);
    item_w  = 10 + (INT32)namelen * UI_FONT_W + 6;
    if (tx + item_w > tray_right) {
      break;
    }

    health = MetalUiNetIfHealth (&cfg);
    fg     = MetalUiNetHealthColor (health);
    pm_metal_gfx_fill_rect (tx + 2, w->y + 9, 6, 6, fg);
    pm_metal_gfx_draw_text (tx + 10, ty, cfg.name, fg, COL_STATUS, 1);
    tx += item_w;
  }

  /* Separator between tray and clock. */
  if (tray_w > 0) {
    pm_metal_gfx_fill_rect (clock_x - 5, w->y + 5, 1, w->h - 10, COL_BEVEL_LO);
    pm_metal_gfx_fill_rect (clock_x - 4, w->y + 5, 1, w->h - 10, COL_BEVEL_HI);
  }

  /* Separated clock field (inset). */
  pm_metal_gfx_fill_rect (clock_x, w->y + 2, clock_w, w->h - 4, COL_STATUS_CLK);
  pm_metal_gfx_bevel_rect (
    clock_x,
    w->y + 2,
    clock_w,
    w->h - 4,
    0,
    COL_BEVEL_LO,
    COL_BEVEL_HI
    );

  ms   = pm_metal_tz_local_ms ();
  tod  = (UINT32)((ms / 1000ull) % 86400ull);
  hour = tod / 3600u;
  min  = (tod % 3600u) / 60u;
  AsciiSPrint (clock, sizeof (clock), "%02u:%02u", hour, min);
  pm_metal_gfx_draw_text (
    clock_x + (clock_w - UI_CLOCK_CHARS * UI_FONT_W) / 2,
    w->y + 4,
    clock,
    (pm_metal_net_ntp_last_unix_ms () != 0ull) ? COL_STATUS_TXT : COL_LOG_WARN,
    COL_STATUS_CLK,
    1
    );

  MetalUiStatusSnapshot (
    &mStatusClockTod,
    &mStatusNetHealth,
    &mStatusIfCount,
    &mStatusNtpBit
    );
}

STATIC
VOID
MetalUiPaintWidget (
  metal_ui_widget_t  *w
  )
{
  metal_ui_widget_t  *c;

  if (w == NULL) {
    return;
  }

  switch (w->kind) {
    case METAL_UI_KIND_WINDOW:
      pm_metal_gfx_fill_rect (w->x, w->y, w->w, w->h, COL_WINDOW);
      pm_metal_gfx_bevel_rect (w->x, w->y, w->w, w->h, 1, COL_BEVEL_HI, COL_BEVEL_LO);
      pm_metal_gfx_fill_rect (w->x + 4, w->y + 4, w->w - 8, UI_TITLE_H - 4, COL_TITLE);
      pm_metal_gfx_draw_text (w->x + 12, w->y + 8, w->title, COL_TITLE_TXT, COL_TITLE, 1);
      break;

    case METAL_UI_KIND_TABS:
      MetalUiPaintTabsStrip (w);
      if (w->u.tabs.n > 0 && w->u.tabs.active < w->u.tabs.n) {
        MetalUiPaintWidget (w->u.tabs.tabs[w->u.tabs.active]);
      }

      return;

    case METAL_UI_KIND_FRAME:
      if (!MetalUiTabGuestOwnsContent (w)) {
        pm_metal_gfx_fill_rect (w->x, w->y, w->w, w->h, COL_FRAME_FACE);
      }

      pm_metal_gfx_bevel_rect (w->x, w->y, w->w, w->h, 0, COL_BEVEL_HI, COL_BEVEL_LO);
      break;

    case METAL_UI_KIND_CONSOLE:
      if (MetalUiTabGuestOwnsContent (w)) {
        /* Windowed guest owns the whole content — no prompt under the game. */
        return;
      }

      MetalUiPaintConsole (w);
      return;

    case METAL_UI_KIND_STATUS_BAR:
      MetalUiPaintStatusBar (w);
      return;

    default:
      break;
  }

  for (c = w->child; c != NULL; c = c->next) {
    if (w->kind == METAL_UI_KIND_WINDOW && c->kind == METAL_UI_KIND_TABS) {
      MetalUiPaintWidget (c);
    } else if (w->kind == METAL_UI_KIND_WINDOW && c->kind == METAL_UI_KIND_STATUS_BAR) {
      MetalUiPaintWidget (c);
    } else if (w->kind != METAL_UI_KIND_WINDOW) {
      MetalUiPaintWidget (c);
    }
  }
}

VOID
MetalUiLayout (
  VOID
  )
{
  pm_metal_gfx_surface_t  *surf;

  surf = pm_metal_gfx_surface ();
  if (gMetalUiShellRoot == NULL || surf == NULL) {
    return;
  }

  MetalUiLayoutWindow (gMetalUiShellRoot, (INT32)surf->width, (INT32)surf->height);
}

VOID
MetalUiPaint (
  VOID
  )
{
  if (gMetalUiShellRoot == NULL) {
    return;
  }

  /*
   * Full clear wipes windowed guest content. When a tab session owns a
   * surface, skip the desktop clear — chrome widgets redraw themselves.
   */
  if (!(pm_metal_wasm_session_active ()
        && pm_metal_wasm_stdout_tab () != PM_METAL_UI_HANDLE_INVALID
        && pm_metal_ui_tab_surface (pm_metal_wasm_stdout_tab ())
             != PM_METAL_GFX_SURFACE_INVALID))
  {
    pm_metal_gfx_clear (COL_DESKTOP);
  }

  MetalUiPaintWidget (gMetalUiShellRoot);
}

INT32
MetalUiStatusNeedsRefresh (
  VOID
  )
{
  UINT32  clock_tod;
  UINT32  net_health;
  UINT32  if_count;
  UINT32  ntp_bit;

  MetalUiStatusSnapshot (&clock_tod, &net_health, &if_count, &ntp_bit);
  if (clock_tod != mStatusClockTod
      || net_health != mStatusNetHealth
      || if_count != mStatusIfCount
      || ntp_bit != mStatusNtpBit)
  {
    return 1;
  }

  return 0;
}
