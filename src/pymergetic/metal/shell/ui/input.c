/** @file
  UI shell input line + pointer hit-test + software cursor + console scroll.
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "priv.h"

#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/shell/lifecycle/lifecycle.h>

static int32_t  mScrollDrag;
static int32_t  mScrollGrabDy; /* pointer Y - thumb_y at press */
static uint32_t mScrollGrabOff;
static uint32_t mPrevLmb;
static int32_t  mInputRelayout; /* visible input rows changed — need full chrome */

/* Classic arrow (hotspot 0,0). 0=clear 1=outline 2=fill */
#define UI_CUR_W 12
#define UI_CUR_H 19

static const uint8_t mCurMask[UI_CUR_H][UI_CUR_W] = {
  { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0 }, { 1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0 },
  { 1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0 }, { 1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0 },
  { 1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0 }, { 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0 },
  { 1, 2, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0 }, { 1, 2, 2, 2, 1, 2, 1, 0, 0, 0, 0, 0 },
  { 1, 2, 2, 1, 0, 1, 2, 1, 0, 0, 0, 0 }, { 1, 2, 1, 0, 0, 1, 2, 1, 0, 0, 0, 0 },
  { 1, 1, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0 }, { 1, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 },
};

static int32_t              mCurLive;
static int32_t              mCurX;
static int32_t              mCurY;
static pm_metal_gfx_color_t mCurUnder[UI_CUR_W * UI_CUR_H];

static void MetalUiInputSyncCaret(void)
{
  metal_ui_widget_t *c;

  c = gMetalUiSysConsole;
  if (c == NULL) {
    return;
  }

  if (c->u.console.input_cursor > c->u.console.input_len) {
    c->u.console.input_cursor = c->u.console.input_len;
  }
}

uint32_t MetalUiInputWrapColsFromWidth(int32_t width_px)
{
  uint32_t fw;
  uint32_t cols;

  fw = pm_metal_gfx_font_width();
  if (fw == 0u) {
    fw = UI_FONT_W;
  }

  if (width_px < (int32_t)fw) {
    return 8u;
  }

  cols = (uint32_t)width_px / fw;
  if (cols < 8u) {
    cols = 8u;
  }

  return cols;
}

/**
 * Wrap columns for the shared input strip (pad + scroll gutter).
 * Uses console width — do not call MetalUiShellInputGeom (avoids recursion).
 */
uint32_t MetalUiInputCurrentWrap(void)
{
  metal_ui_widget_t *con;
  int32_t            text_w;

  con = MetalUiActiveConsole();
  if (con == NULL) {
    con = gMetalUiSysConsole;
  }

  if (con == NULL || con->w <= 0) {
    return 40u;
  }

  text_w = con->w - 4 - UI_SCROLL_W;
  if (text_w < 8) {
    text_w = con->w > 4 ? con->w - 4 : con->w;
  }

  return MetalUiInputWrapColsFromWidth(text_w);
}

static void MetalUiInputNoteHeight(uint32_t before_vis)
{
  uint32_t wrap;

  wrap = MetalUiInputCurrentWrap();
  if (MetalUiInputVisibleRows(wrap) != before_vis) {
    mInputRelayout = 1;
  }
}

int pm_metal_ui_input_consume_relayout(void)
{
  int32_t d;

  d              = mInputRelayout;
  mInputRelayout = 0;
  return d ? 1 : 0;
}

/**
 * Walk buffer; if want_idx == UINT32_MAX, return total visual rows.
 * Else write out_row and out_col for byte want_idx; return rows so far + 1.
 */
static uint32_t MetalUiInputWalk(uint32_t  wrap_cols,
                                 uint32_t  want_idx,
                                 uint32_t *out_row,
                                 uint32_t *out_col)
{
  metal_ui_widget_t *c;
  uint32_t           i;
  uint32_t           row;
  uint32_t           col;
  uint32_t           len;

  c = gMetalUiSysConsole;
  if (c == NULL || wrap_cols == 0u) {
    if (out_row != NULL) {
      *out_row = 0;
    }

    if (out_col != NULL) {
      *out_col = 0;
    }

    return 1u;
  }

  len = c->u.console.input_len;
  row = 0;
  col = 0;
  for (i = 0; i <= len; i++) {
    if (want_idx != ((uint32_t)-1) && i == want_idx) {
      if (out_row != NULL) {
        *out_row = row;
      }

      if (out_col != NULL) {
        *out_col = col;
      }
    }

    if (i == len) {
      break;
    }

    if (c->u.console.input[i] == '\n') {
      row++;
      col = 0;
    } else {
      if (col >= wrap_cols) {
        row++;
        col = 0;
      }

      col++;
    }
  }

  return row + 1u;
}

uint32_t MetalUiInputVisualRows(uint32_t wrap_cols)
{
  return MetalUiInputWalk(wrap_cols, (uint32_t)-1, NULL, NULL);
}

uint32_t MetalUiInputVisibleRows(uint32_t wrap_cols)
{
  uint32_t v;

  v = MetalUiInputVisualRows(wrap_cols);
  if (v < 1u) {
    v = 1u;
  }

  if (v > UI_INPUT_ROWS_MAX) {
    v = UI_INPUT_ROWS_MAX;
  }

  return v;
}

uint32_t MetalUiInputCaretRow(uint32_t wrap_cols)
{
  uint32_t row;

  row = 0;
  MetalUiInputCaretCell(wrap_cols, &row, NULL);
  return row;
}

void MetalUiInputCaretCell(uint32_t wrap_cols, uint32_t *row, uint32_t *col)
{
  uint32_t want;

  want = 0;
  if (gMetalUiSysConsole != NULL) {
    want = gMetalUiSysConsole->u.console.input_cursor;
  }

  (void)MetalUiInputWalk(wrap_cols, want, row, col);
}

void MetalUiInputClampView(uint32_t wrap_cols)
{
  metal_ui_widget_t *c;
  uint32_t           visual;
  uint32_t           visible;
  uint32_t           max_off;

  c = gMetalUiSysConsole;
  if (c == NULL) {
    return;
  }

  visual  = MetalUiInputVisualRows(wrap_cols);
  visible = MetalUiInputVisibleRows(wrap_cols);
  max_off = (visual > visible) ? (visual - visible) : 0u;
  if (c->u.console.input_view_off > max_off) {
    c->u.console.input_view_off = max_off;
  }
}

void MetalUiInputEnsureCaretVisible(uint32_t wrap_cols)
{
  metal_ui_widget_t *c;
  uint32_t           visual;
  uint32_t           visible;
  uint32_t           caret;
  uint32_t           first;
  uint32_t           max_off;

  c = gMetalUiSysConsole;
  if (c == NULL) {
    return;
  }

  visual  = MetalUiInputVisualRows(wrap_cols);
  visible = MetalUiInputVisibleRows(wrap_cols);
  caret   = MetalUiInputCaretRow(wrap_cols);
  max_off = (visual > visible) ? (visual - visible) : 0u;

  /* first visible row from top */
  if (visual <= visible) {
    c->u.console.input_view_off = 0;
    return;
  }

  first = visual - visible - c->u.console.input_view_off;
  if (caret < first) {
    c->u.console.input_view_off = visual - visible - caret;
  } else if (caret >= first + visible) {
    c->u.console.input_view_off = visual - visible - (caret - visible + 1u);
  }

  if (c->u.console.input_view_off > max_off) {
    c->u.console.input_view_off = max_off;
  }
}

void MetalUiInputScrollBy(int32_t delta_rows, uint32_t wrap_cols)
{
  metal_ui_widget_t *c;
  int32_t            off;

  c = gMetalUiSysConsole;
  if (c == NULL || delta_rows == 0) {
    return;
  }

  off = (int32_t)c->u.console.input_view_off + delta_rows;
  if (off < 0) {
    off = 0;
  }

  c->u.console.input_view_off = (uint32_t)off;
  MetalUiInputClampView(wrap_cols);
}

int32_t MetalUiInputMoveCursor(int32_t delta)
{
  metal_ui_widget_t *c;
  int32_t            cur;

  c = gMetalUiSysConsole;
  if (c == NULL) {
    return -1;
  }

  cur = (int32_t)c->u.console.input_cursor + delta;
  if (cur < 0) {
    cur = 0;
  }

  if (cur > (int32_t)c->u.console.input_len) {
    cur = (int32_t)c->u.console.input_len;
  }

  c->u.console.input_cursor = (uint32_t)cur;
  return 0;
}

int pm_metal_ui_input_move_cursor(int delta)
{
  uint32_t wrap;

  if (MetalUiInputMoveCursor(delta) != 0) {
    return -1;
  }

  wrap = MetalUiInputCurrentWrap();
  MetalUiInputEnsureCaretVisible(wrap);
  return 0;
}

int pm_metal_ui_input_move_visual_row(int delta_rows)
{
  return MetalUiInputMoveVisualRow(delta_rows, MetalUiInputCurrentWrap());
}

/**
 * Byte index at start of visual row @a target_row, column clamped to @a want_col.
 */
static uint32_t MetalUiInputIndexAt(uint32_t wrap_cols, uint32_t target_row, uint32_t want_col)
{
  metal_ui_widget_t *c;
  uint32_t           i;
  uint32_t           row;
  uint32_t           col;
  uint32_t           len;
  uint32_t           row_start;

  c = gMetalUiSysConsole;
  if (c == NULL || wrap_cols == 0u) {
    return 0;
  }

  len       = c->u.console.input_len;
  row       = 0;
  col       = 0;
  row_start = 0;
  for (i = 0; i <= len; i++) {
    if (row == target_row && col == 0) {
      row_start = i;
    }

    if (row == target_row && col == want_col) {
      return i;
    }

    if (i == len) {
      break;
    }

    if (c->u.console.input[i] == '\n') {
      if (row == target_row) {
        return i; /* end of that line */
      }

      row++;
      col = 0;
    } else {
      if (col >= wrap_cols) {
        if (row == target_row) {
          return i;
        }

        row++;
        col = 0;
        if (row == target_row) {
          row_start = i;
        }
      }

      col++;
    }
  }

  (void)row_start;
  return len;
}

int32_t MetalUiInputMoveVisualRow(int32_t delta_rows, uint32_t wrap_cols)
{
  metal_ui_widget_t *c;
  uint32_t           row;
  uint32_t           col;
  uint32_t           visual;
  int32_t            dest;

  c = gMetalUiSysConsole;
  if (c == NULL || delta_rows == 0) {
    return 0;
  }

  row = 0;
  col = 0;
  (void)MetalUiInputWalk(wrap_cols, c->u.console.input_cursor, &row, &col);
  visual = MetalUiInputVisualRows(wrap_cols);
  dest   = (int32_t)row + delta_rows;
  if (dest < 0 || (uint32_t)dest >= visual) {
    return 0;
  }

  c->u.console.input_cursor = MetalUiInputIndexAt(wrap_cols, (uint32_t)dest, col);
  MetalUiInputEnsureCaretVisible(wrap_cols);
  return 1;
}

void pm_metal_ui_input_clear(void)
{
  if (gMetalUiSysConsole == NULL) {
    return;
  }

  memset(gMetalUiSysConsole->u.console.input, 0, sizeof(gMetalUiSysConsole->u.console.input));
  gMetalUiSysConsole->u.console.input_len      = 0;
  gMetalUiSysConsole->u.console.input_cursor   = 0;
  gMetalUiSysConsole->u.console.input_view_off = 0;
}

int pm_metal_ui_input_append(char ch)
{
  metal_ui_widget_t *c;
  uint32_t           i;
  uint32_t           cur;
  uint32_t           wrap;
  uint32_t           before;

  c = gMetalUiSysConsole;
  if (c == NULL) {
    return -1;
  }

  /* Allow ASCII 32-126 plus Latin-15 0x80-0xFF (`keyb gr` umlauts/ss/etc,
   * see keyb.c) -- only '\n' and DEL (0x7F) are excluded. */
  if (ch != '\n' && ((uint8_t)ch < 32 || (uint8_t)ch == 0x7fu)) {
    return -1;
  }

  if (c->u.console.input_len + 1u >= INPUT_CHARS) {
    return -1;
  }

  wrap   = MetalUiInputCurrentWrap();
  before = MetalUiInputVisibleRows(wrap);
  MetalUiInputSyncCaret();
  cur = c->u.console.input_cursor;
  for (i = c->u.console.input_len; i > cur; i--) {
    c->u.console.input[i] = c->u.console.input[i - 1u];
  }

  c->u.console.input[cur] = ch;
  c->u.console.input_len++;
  c->u.console.input_cursor                  = cur + 1u;
  c->u.console.input[c->u.console.input_len] = '\0';
  MetalUiInputEnsureCaretVisible(wrap);
  MetalUiInputNoteHeight(before);
  return 0;
}

int pm_metal_ui_input_backspace(void)
{
  metal_ui_widget_t *c;
  uint32_t           i;
  uint32_t           cur;
  uint32_t           wrap;
  uint32_t           before;

  c = gMetalUiSysConsole;
  if (c == NULL) {
    return -1;
  }

  MetalUiInputSyncCaret();
  cur = c->u.console.input_cursor;
  if (cur == 0u) {
    return -1;
  }

  wrap   = MetalUiInputCurrentWrap();
  before = MetalUiInputVisibleRows(wrap);
  for (i = cur - 1u; i < c->u.console.input_len; i++) {
    c->u.console.input[i] = c->u.console.input[i + 1u];
  }

  c->u.console.input_len--;
  c->u.console.input_cursor                  = cur - 1u;
  c->u.console.input[c->u.console.input_len] = '\0';
  MetalUiInputEnsureCaretVisible(wrap);
  MetalUiInputNoteHeight(before);
  return 0;
}

int pm_metal_ui_input_delete_fwd(void)
{
  metal_ui_widget_t *c;
  uint32_t           i;
  uint32_t           cur;
  uint32_t           wrap;
  uint32_t           before;

  c = gMetalUiSysConsole;
  if (c == NULL) {
    return -1;
  }

  MetalUiInputSyncCaret();
  cur = c->u.console.input_cursor;
  if (cur >= c->u.console.input_len) {
    return -1;
  }

  wrap   = MetalUiInputCurrentWrap();
  before = MetalUiInputVisibleRows(wrap);
  for (i = cur; i < c->u.console.input_len; i++) {
    c->u.console.input[i] = c->u.console.input[i + 1u];
  }

  c->u.console.input_len--;
  c->u.console.input[c->u.console.input_len] = '\0';
  MetalUiInputEnsureCaretVisible(wrap);
  MetalUiInputNoteHeight(before);
  return 0;
}

int pm_metal_ui_input_text(char *out, uint32_t cap)
{
  uint32_t n;
  uint32_t i;

  if (out == NULL || cap == 0) {
    return -1;
  }

  if (gMetalUiSysConsole == NULL) {
    out[0] = '\0';
    return 0;
  }

  n = gMetalUiSysConsole->u.console.input_len;
  if (n + 1 > cap) {
    n = cap - 1;
  }

  for (i = 0; i < n; i++) {
    out[i] = gMetalUiSysConsole->u.console.input[i];
  }

  out[n] = '\0';
  return (int)n;
}

int pm_metal_ui_input_set(const char *text)
{
  uint32_t n;
  uint32_t i;

  if (gMetalUiSysConsole == NULL) {
    return -1;
  }

  if (text == NULL) {
    text = "";
  }

  n = 0;
  while (text[n] != '\0' && n + 1u < INPUT_CHARS) {
    n++;
  }

  for (i = 0; i < n; i++) {
    gMetalUiSysConsole->u.console.input[i] = text[i];
  }

  gMetalUiSysConsole->u.console.input[n]       = '\0';
  gMetalUiSysConsole->u.console.input_len      = n;
  gMetalUiSysConsole->u.console.input_cursor   = n;
  gMetalUiSysConsole->u.console.input_view_off = 0;
  return (int)n;
}

uint32_t MetalUiInputVisualRowText(uint32_t wrap_cols,
                                   uint32_t visual_row,
                                   char    *out,
                                   uint32_t out_cap)
{
  metal_ui_widget_t *c;
  uint32_t           i;
  uint32_t           row;
  uint32_t           col;
  uint32_t           len;
  uint32_t           n;

  if (out == NULL || out_cap == 0u) {
    return 0;
  }

  out[0] = '\0';
  c      = gMetalUiSysConsole;
  if (c == NULL || wrap_cols == 0u) {
    return 0;
  }

  len = c->u.console.input_len;
  row = 0;
  col = 0;
  n   = 0;
  for (i = 0; i < len; i++) {
    char ch;

    ch = c->u.console.input[i];
    if (ch == '\n') {
      if (row == visual_row) {
        break;
      }

      row++;
      col = 0;
      continue;
    }

    if (col >= wrap_cols) {
      if (row == visual_row) {
        break;
      }

      row++;
      col = 0;
    }

    if (row == visual_row) {
      if (n + 1u < out_cap) {
        out[n++] = ch;
      }
    }

    col++;
  }

  out[n] = '\0';
  return n;
}

int pm_metal_ui_pointer_hit(int32_t x, int32_t y)
{
  int32_t            idx;
  metal_ui_widget_t *hit;

  idx = MetalUiTabIndexAt(x, y);
  if (idx < 0 || gMetalUiTabs == NULL) {
    return 0;
  }

  gMetalUiTabs->u.tabs.active = (uint32_t)idx;
  hit                         = gMetalUiTabs->u.tabs.tabs[idx];
  if (hit != NULL) {
    pm_metal_lifecycle_set(hit->surface != 0 ? hit->surface : PM_METAL_GFX_SURFACE_DEFAULT,
                           PM_METAL_LIFE_FOCUSED | PM_METAL_LIFE_VISIBLE);
  }

  pm_metal_ui_sync_input_focus();
  return 1;
}

int pm_metal_ui_pointer_hover(int32_t x, int32_t y)
{
  int32_t idx;
  int32_t prev;

  if (gMetalUiTabs == NULL) {
    return 0;
  }

  idx  = MetalUiTabIndexAt(x, y);
  prev = gMetalUiTabs->u.tabs.hover;
  if (idx == prev) {
    return 0;
  }

  gMetalUiTabs->u.tabs.hover = idx;
  return 1;
}

void pm_metal_ui_console_scroll_by(int32_t delta_lines)
{
  MetalUiConsoleScrollBy(MetalUiActiveConsole(), delta_lines);
}

void pm_metal_ui_console_scroll_page(int32_t dir)
{
  metal_ui_widget_t *con;
  uint32_t           rows;

  con  = MetalUiActiveConsole();
  rows = MetalUiConsoleVisibleRows(con);
  if (rows > 1u) {
    rows--;
  }

  if (rows == 0u) {
    rows = 1u;
  }

  MetalUiConsoleScrollBy(con, (dir > 0) ? (int32_t)rows : -(int32_t)rows);
}

static uint32_t ConsoleViewOffFromThumbY(
  metal_ui_widget_t *con, int32_t thumb_y, int32_t thumb_h, int32_t track_y, int32_t track_h)
{
  uint32_t max_off;
  int32_t  travel;
  int32_t  pos;

  max_off = MetalUiConsoleMaxOff(con);
  if (max_off == 0) {
    return 0;
  }

  travel = track_h - thumb_h;
  if (travel <= 0) {
    return 0;
  }

  /* Invert: top of track = max_off, bottom = 0 */
  pos = (track_y + travel) - thumb_y;
  if (pos < 0) {
    pos = 0;
  }

  if (pos > travel) {
    pos = travel;
  }

  return (uint32_t)((uint64_t)pos * (uint64_t)max_off / (uint64_t)travel);
}

int pm_metal_ui_console_pointer(
  int32_t x, int32_t y, uint32_t buttons, int32_t wheel, uint32_t flags)
{
  metal_ui_widget_t *con;
  int32_t            trx;
  int32_t            try;
  int32_t            trw;
  int32_t            trh;
  int32_t            thy;
  int32_t            thh;
  uint32_t           lmb;
  int32_t            dirty;
  int32_t            have_bar;

  con   = MetalUiActiveConsole();
  dirty = 0;
  lmb   = buttons & 1u;

  if (con == NULL) {
    mScrollDrag = 0;
    mPrevLmb    = lmb;
    return 0;
  }

  if ((flags & PM_METAL_INPUT_PTR_WHEEL) != 0 && wheel != 0) {
    int32_t  ix;
    int32_t  iy;
    int32_t  iw;
    int32_t  ih;
    uint32_t wrap;

    /* Wheel over input strip scrolls input; else console history. */
    if (MetalUiShellInputGeom(&ix, &iy, &iw, &ih) == 0 && x >= ix && x < ix + iw && y >= iy &&
        y < iy + ih) {
      wrap = MetalUiInputCurrentWrap();
      MetalUiInputScrollBy(wheel, wrap);
    } else {
      /* Positive wheel → older history. */
      MetalUiConsoleScrollBy(con, wheel);
    }

    dirty = 1;
  }

  have_bar = MetalUiConsoleScrollBarGeom(con, &trx, &try, &trw, &trh, &thy, &thh);

  if (mScrollDrag) {
    if (lmb == 0) {
      mScrollDrag = 0;
    } else if (have_bar) {
      int32_t  new_thy;
      uint32_t off;

      new_thy = y - mScrollGrabDy;
      if (new_thy < try) {
        new_thy = try;
      }

      if (new_thy + thh > try + trh) {
        new_thy = try + trh - thh;
      }

      off = ConsoleViewOffFromThumbY(con, new_thy, thh, try, trh);
      if (off != con->u.console.view_off) {
        MetalUiConsoleScrollTo(con, off);
        dirty = 1;
      }
    }

    mPrevLmb = lmb;
    return dirty ? 1 : 0;
  }

  if (lmb != 0 && mPrevLmb == 0 && have_bar && x >= trx && x < trx + trw && y >= try &&
      y < try + trh) {
    if (y >= thy && y < thy + thh) {
      mScrollDrag    = 1;
      mScrollGrabDy  = y - thy;
      mScrollGrabOff = con->u.console.view_off;
      (void)mScrollGrabOff;
    } else {
      /* Track click — jump thumb so click is centered on thumb. */
      int32_t  new_thy;
      uint32_t off;

      new_thy = y - thh / 2;
      if (new_thy < try) {
        new_thy = try;
      }

      if (new_thy + thh > try + trh) {
        new_thy = try + trh - thh;
      }

      off = ConsoleViewOffFromThumbY(con, new_thy, thh, try, trh);
      MetalUiConsoleScrollTo(con, off);
      mScrollDrag   = 1;
      mScrollGrabDy = y - new_thy;
      dirty         = 1;
    }
  }

  mPrevLmb = lmb;
  return dirty ? 1 : 0;
}

static void MetalUiCursorBounds(
  int32_t x, int32_t y, int32_t *ox, int32_t *oy, int32_t *ow, int32_t *oh)
{
  pm_metal_gfx_surface_t *surf;
  int32_t                 x0;
  int32_t                 y0;
  int32_t                 x1;
  int32_t                 y1;

  surf = pm_metal_gfx_surface();
  if (surf == NULL || surf->pixels == NULL) {
    *ox = 0;
    *oy = 0;
    *ow = 0;
    *oh = 0;
    return;
  }

  x0 = x;
  y0 = y;
  x1 = x + UI_CUR_W;
  y1 = y + UI_CUR_H;
  if (x0 < 0) {
    x0 = 0;
  }

  if (y0 < 0) {
    y0 = 0;
  }

  if (x1 > (int32_t)surf->width) {
    x1 = (int32_t)surf->width;
  }

  if (y1 > (int32_t)surf->height) {
    y1 = (int32_t)surf->height;
  }

  *ox = x0;
  *oy = y0;
  *ow = (x1 > x0) ? (x1 - x0) : 0;
  *oh = (y1 > y0) ? (y1 - y0) : 0;
}

static void MetalUiCursorSaveUnder(int32_t x, int32_t y)
{
  pm_metal_gfx_surface_t *surf;
  int32_t                 row;
  int32_t                 col;

  surf = pm_metal_gfx_surface();
  if (surf == NULL || surf->pixels == NULL) {
    return;
  }

  for (row = 0; row < UI_CUR_H; row++) {
    for (col = 0; col < UI_CUR_W; col++) {
      int32_t px;
      int32_t py;

      px = x + col;
      py = y + row;
      if (px < 0 || py < 0 || (uint32_t)px >= surf->width || (uint32_t)py >= surf->height) {
        mCurUnder[row * UI_CUR_W + col] = 0;
        continue;
      }

      mCurUnder[row * UI_CUR_W + col] = surf->pixels[(uint32_t)py * surf->pitch + (uint32_t)px];
    }
  }
}

static void MetalUiCursorRestoreUnder(void)
{
  pm_metal_gfx_surface_t *surf;
  int32_t                 row;
  int32_t                 col;

  if (!mCurLive) {
    return;
  }

  surf = pm_metal_gfx_surface();
  if (surf == NULL || surf->pixels == NULL) {
    return;
  }

  for (row = 0; row < UI_CUR_H; row++) {
    for (col = 0; col < UI_CUR_W; col++) {
      int32_t px;
      int32_t py;

      if (mCurMask[row][col] == 0) {
        continue;
      }

      px = mCurX + col;
      py = mCurY + row;
      if (px < 0 || py < 0 || (uint32_t)px >= surf->width || (uint32_t)py >= surf->height) {
        continue;
      }

      surf->pixels[(uint32_t)py * surf->pitch + (uint32_t)px] = mCurUnder[row * UI_CUR_W + col];
    }
  }
}

static void MetalUiCursorBlit(int32_t x, int32_t y)
{
  pm_metal_gfx_surface_t *surf;
  int32_t                 row;
  int32_t                 col;

  surf = pm_metal_gfx_surface();
  if (surf == NULL || surf->pixels == NULL) {
    return;
  }

  for (row = 0; row < UI_CUR_H; row++) {
    for (col = 0; col < UI_CUR_W; col++) {
      uint8_t              cell;
      pm_metal_gfx_color_t c;
      int32_t              px;
      int32_t              py;

      cell = mCurMask[row][col];
      if (cell == 0) {
        continue;
      }

      c  = (cell == 1) ? PM_METAL_GFX_RGB(0x00, 0x00, 0x00) : PM_METAL_GFX_RGB(0xff, 0xff, 0xff);
      px = x + col;
      py = y + row;
      if (px < 0 || py < 0 || (uint32_t)px >= surf->width || (uint32_t)py >= surf->height) {
        continue;
      }

      surf->pixels[(uint32_t)py * surf->pitch + (uint32_t)px] = c;
    }
  }
}

void pm_metal_ui_cursor_invalidate(void)
{
  mCurLive = 0;
}

void pm_metal_ui_cursor_hide(void)
{
  int32_t ox;
  int32_t oy;
  int32_t ow;
  int32_t oh;

  if (!mCurLive) {
    return;
  }

  MetalUiCursorBounds(mCurX, mCurY, &ox, &oy, &ow, &oh);
  MetalUiCursorRestoreUnder();
  mCurLive = 0;
  if (ow > 0 && oh > 0) {
    (void)pm_metal_gfx_present_rect(ox, oy, ow, oh);
  }
}

void pm_metal_ui_cursor_paint(int32_t x, int32_t y)
{
  /* Shadow FB already holds chrome; capture under then stamp. No present. */
  mCurLive = 0;
  MetalUiCursorSaveUnder(x, y);
  MetalUiCursorBlit(x, y);
  mCurX    = x;
  mCurY    = y;
  mCurLive = 1;
}

void pm_metal_ui_cursor_move(int32_t x, int32_t y)
{
  int32_t ox0;
  int32_t oy0;
  int32_t ow0;
  int32_t oh0;
  int32_t ox1;
  int32_t oy1;
  int32_t ow1;
  int32_t oh1;
  int32_t ux;
  int32_t uy;
  int32_t uw;
  int32_t uh;

  if (mCurLive && mCurX == x && mCurY == y) {
    return;
  }

  ox0 = oy0 = ow0 = oh0 = 0;
  if (mCurLive) {
    MetalUiCursorBounds(mCurX, mCurY, &ox0, &oy0, &ow0, &oh0);
    MetalUiCursorRestoreUnder();
    mCurLive = 0;
  }

  MetalUiCursorSaveUnder(x, y);
  MetalUiCursorBlit(x, y);
  mCurX    = x;
  mCurY    = y;
  mCurLive = 1;
  MetalUiCursorBounds(x, y, &ox1, &oy1, &ow1, &oh1);

  if (ow0 <= 0 || oh0 <= 0) {
    if (ow1 > 0 && oh1 > 0) {
      (void)pm_metal_gfx_present_rect(ox1, oy1, ow1, oh1);
    }

    return;
  }

  if (ow1 <= 0 || oh1 <= 0) {
    (void)pm_metal_gfx_present_rect(ox0, oy0, ow0, oh0);
    return;
  }

  ux = (ox0 < ox1) ? ox0 : ox1;
  uy = (oy0 < oy1) ? oy0 : oy1;
  uw = ((ox0 + ow0) > (ox1 + ow1) ? (ox0 + ow0) : (ox1 + ow1)) - ux;
  uh = ((oy0 + oh0) > (oy1 + oh1) ? (oy0 + oh0) : (oy1 + oh1)) - uy;
  (void)pm_metal_gfx_present_rect(ux, uy, uw, uh);
}
