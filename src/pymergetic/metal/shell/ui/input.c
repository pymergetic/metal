/** @file
  UI shell input line + pointer hit-test + software cursor + console scroll.
**/
#include "priv.h"

#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/shell/lifecycle/lifecycle.h>

#include <Library/BaseMemoryLib.h>

STATIC INT32   mScrollDrag;
STATIC INT32   mScrollGrabDy;  /* pointer Y - thumb_y at press */
STATIC UINT32  mScrollGrabOff;
STATIC UINT32  mPrevLmb;

/* Classic arrow (hotspot 0,0). 0=clear 1=outline 2=fill */
#define UI_CUR_W  12
#define UI_CUR_H  19

STATIC CONST UINT8  mCurMask[UI_CUR_H][UI_CUR_W] = {
  { 1,0,0,0,0,0,0,0,0,0,0,0 },
  { 1,1,0,0,0,0,0,0,0,0,0,0 },
  { 1,2,1,0,0,0,0,0,0,0,0,0 },
  { 1,2,2,1,0,0,0,0,0,0,0,0 },
  { 1,2,2,2,1,0,0,0,0,0,0,0 },
  { 1,2,2,2,2,1,0,0,0,0,0,0 },
  { 1,2,2,2,2,2,1,0,0,0,0,0 },
  { 1,2,2,2,2,2,2,1,0,0,0,0 },
  { 1,2,2,2,2,2,2,2,1,0,0,0 },
  { 1,2,2,2,2,2,2,2,2,1,0,0 },
  { 1,2,2,2,2,2,1,1,1,1,0,0 },
  { 1,2,2,2,1,2,1,0,0,0,0,0 },
  { 1,2,2,1,0,1,2,1,0,0,0,0 },
  { 1,2,1,0,0,1,2,1,0,0,0,0 },
  { 1,1,0,0,0,0,1,2,1,0,0,0 },
  { 1,0,0,0,0,0,1,2,1,0,0,0 },
  { 0,0,0,0,0,0,0,1,2,1,0,0 },
  { 0,0,0,0,0,0,0,1,2,1,0,0 },
  { 0,0,0,0,0,0,0,0,1,0,0,0 },
};

STATIC INT32                 mCurLive;
STATIC INT32                 mCurX;
STATIC INT32                 mCurY;
STATIC pm_metal_gfx_color_t  mCurUnder[UI_CUR_W * UI_CUR_H];

void
pm_metal_ui_input_clear (
  void
  )
{
  if (gMetalUiSysConsole == NULL) {
    return;
  }

  ZeroMem (gMetalUiSysConsole->u.console.input, sizeof (gMetalUiSysConsole->u.console.input));
  gMetalUiSysConsole->u.console.input_len = 0;
}

int
pm_metal_ui_input_append (
  char  ch
  )
{
  if (gMetalUiSysConsole == NULL || ch < 32) {
    return -1;
  }

  if (gMetalUiSysConsole->u.console.input_len + 1 >= INPUT_CHARS) {
    return -1;
  }

  gMetalUiSysConsole->u.console.input[gMetalUiSysConsole->u.console.input_len++] = ch;
  gMetalUiSysConsole->u.console.input[gMetalUiSysConsole->u.console.input_len]   = '\0';
  return 0;
}

int
pm_metal_ui_input_backspace (
  void
  )
{
  if (gMetalUiSysConsole == NULL || gMetalUiSysConsole->u.console.input_len == 0) {
    return -1;
  }

  gMetalUiSysConsole->u.console.input_len--;
  gMetalUiSysConsole->u.console.input[gMetalUiSysConsole->u.console.input_len] = '\0';
  return 0;
}

int
pm_metal_ui_input_text (
  char      *out,
  uint32_t   cap
  )
{
  UINT32  n;
  UINT32  i;

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

int
pm_metal_ui_input_set (
  const char  *text
  )
{
  UINT32  n;
  UINT32  i;

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

  gMetalUiSysConsole->u.console.input[n]     = '\0';
  gMetalUiSysConsole->u.console.input_len   = n;
  return (int)n;
}

int
pm_metal_ui_pointer_hit (
  int32_t  x,
  int32_t  y
  )
{
  INT32               idx;
  metal_ui_widget_t  *hit;

  idx = MetalUiTabIndexAt (x, y);
  if (idx < 0 || gMetalUiTabs == NULL) {
    return 0;
  }

  gMetalUiTabs->u.tabs.active = (UINT32)idx;
  hit = gMetalUiTabs->u.tabs.tabs[idx];
  if (hit != NULL) {
    pm_metal_lifecycle_set (
      hit->surface != 0 ? hit->surface : PM_METAL_GFX_SURFACE_DEFAULT,
      PM_METAL_LIFE_FOCUSED | PM_METAL_LIFE_VISIBLE
      );
  }

  pm_metal_ui_sync_input_focus ();
  return 1;
}

int
pm_metal_ui_pointer_hover (
  int32_t  x,
  int32_t  y
  )
{
  INT32  idx;
  INT32  prev;

  if (gMetalUiTabs == NULL) {
    return 0;
  }

  idx  = MetalUiTabIndexAt (x, y);
  prev = gMetalUiTabs->u.tabs.hover;
  if (idx == prev) {
    return 0;
  }

  gMetalUiTabs->u.tabs.hover = idx;
  return 1;
}

void
pm_metal_ui_console_scroll_by (
  int32_t  delta_lines
  )
{
  MetalUiConsoleScrollBy (MetalUiActiveConsole (), delta_lines);
}

void
pm_metal_ui_console_scroll_page (
  int32_t  dir
  )
{
  metal_ui_widget_t  *con;
  UINT32              rows;

  con = MetalUiActiveConsole ();
  rows = MetalUiConsoleVisibleRows (con);
  if (rows > 1u) {
    rows--;
  }

  if (rows == 0u) {
    rows = 1u;
  }

  MetalUiConsoleScrollBy (
    con,
    (dir > 0) ? (INT32)rows : -(INT32)rows
    );
}

STATIC
UINT32
ConsoleViewOffFromThumbY (
  metal_ui_widget_t  *con,
  INT32               thumb_y,
  INT32               thumb_h,
  INT32               track_y,
  INT32               track_h
  )
{
  UINT32  max_off;
  INT32   travel;
  INT32   pos;

  max_off = MetalUiConsoleMaxOff (con);
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

  return (UINT32)((UINT64)pos * (UINT64)max_off / (UINT64)travel);
}

int
pm_metal_ui_console_pointer (
  int32_t   x,
  int32_t   y,
  uint32_t  buttons,
  int32_t   wheel,
  uint32_t  flags
  )
{
  metal_ui_widget_t  *con;
  INT32               trx;
  INT32               try;
  INT32               trw;
  INT32               trh;
  INT32               thy;
  INT32               thh;
  UINT32              lmb;
  INT32               dirty;
  INT32               have_bar;

  con   = MetalUiActiveConsole ();
  dirty = 0;
  lmb   = buttons & 1u;

  if (con == NULL) {
    mScrollDrag = 0;
    mPrevLmb    = lmb;
    return 0;
  }

  if ((flags & PM_METAL_INPUT_PTR_WHEEL) != 0 && wheel != 0) {
    /* Positive wheel → older history. */
    MetalUiConsoleScrollBy (con, wheel);
    dirty = 1;
  }

  have_bar = MetalUiConsoleScrollBarGeom (
               con,
               &trx,
               &try,
               &trw,
               &trh,
               &thy,
               &thh
               );

  if (mScrollDrag) {
    if (lmb == 0) {
      mScrollDrag = 0;
    } else if (have_bar) {
      INT32   new_thy;
      UINT32  off;

      new_thy = y - mScrollGrabDy;
      if (new_thy < try) {
        new_thy = try;
      }

      if (new_thy + thh > try + trh) {
        new_thy = try + trh - thh;
      }

      off = ConsoleViewOffFromThumbY (con, new_thy, thh, try, trh);
      if (off != con->u.console.view_off) {
        MetalUiConsoleScrollTo (con, off);
        dirty = 1;
      }
    }

    mPrevLmb = lmb;
    return dirty ? 1 : 0;
  }

  if (lmb != 0 && mPrevLmb == 0 && have_bar
      && x >= trx && x < trx + trw && y >= try && y < try + trh)
  {
    if (y >= thy && y < thy + thh) {
      mScrollDrag    = 1;
      mScrollGrabDy  = y - thy;
      mScrollGrabOff = con->u.console.view_off;
      (VOID)mScrollGrabOff;
    } else {
      /* Track click — jump thumb so click is centered on thumb. */
      INT32   new_thy;
      UINT32  off;

      new_thy = y - thh / 2;
      if (new_thy < try) {
        new_thy = try;
      }

      if (new_thy + thh > try + trh) {
        new_thy = try + trh - thh;
      }

      off = ConsoleViewOffFromThumbY (con, new_thy, thh, try, trh);
      MetalUiConsoleScrollTo (con, off);
      mScrollDrag   = 1;
      mScrollGrabDy = y - new_thy;
      dirty         = 1;
    }
  }

  mPrevLmb = lmb;
  return dirty ? 1 : 0;
}

STATIC
VOID
MetalUiCursorBounds (
  INT32  x,
  INT32  y,
  INT32  *ox,
  INT32  *oy,
  INT32  *ow,
  INT32  *oh
  )
{
  pm_metal_gfx_surface_t  *surf;
  INT32                    x0;
  INT32                    y0;
  INT32                    x1;
  INT32                    y1;

  surf = pm_metal_gfx_surface ();
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

  if (x1 > (INT32)surf->width) {
    x1 = (INT32)surf->width;
  }

  if (y1 > (INT32)surf->height) {
    y1 = (INT32)surf->height;
  }

  *ox = x0;
  *oy = y0;
  *ow = (x1 > x0) ? (x1 - x0) : 0;
  *oh = (y1 > y0) ? (y1 - y0) : 0;
}

STATIC
VOID
MetalUiCursorSaveUnder (
  INT32  x,
  INT32  y
  )
{
  pm_metal_gfx_surface_t  *surf;
  INT32                    row;
  INT32                    col;

  surf = pm_metal_gfx_surface ();
  if (surf == NULL || surf->pixels == NULL) {
    return;
  }

  for (row = 0; row < UI_CUR_H; row++) {
    for (col = 0; col < UI_CUR_W; col++) {
      INT32  px;
      INT32  py;

      px = x + col;
      py = y + row;
      if (px < 0 || py < 0
          || (UINT32)px >= surf->width
          || (UINT32)py >= surf->height)
      {
        mCurUnder[row * UI_CUR_W + col] = 0;
        continue;
      }

      mCurUnder[row * UI_CUR_W + col] =
        surf->pixels[(UINT32)py * surf->pitch + (UINT32)px];
    }
  }
}

STATIC
VOID
MetalUiCursorRestoreUnder (
  VOID
  )
{
  pm_metal_gfx_surface_t  *surf;
  INT32                    row;
  INT32                    col;

  if (!mCurLive) {
    return;
  }

  surf = pm_metal_gfx_surface ();
  if (surf == NULL || surf->pixels == NULL) {
    return;
  }

  for (row = 0; row < UI_CUR_H; row++) {
    for (col = 0; col < UI_CUR_W; col++) {
      INT32  px;
      INT32  py;

      if (mCurMask[row][col] == 0) {
        continue;
      }

      px = mCurX + col;
      py = mCurY + row;
      if (px < 0 || py < 0
          || (UINT32)px >= surf->width
          || (UINT32)py >= surf->height)
      {
        continue;
      }

      surf->pixels[(UINT32)py * surf->pitch + (UINT32)px] =
        mCurUnder[row * UI_CUR_W + col];
    }
  }
}

STATIC
VOID
MetalUiCursorBlit (
  INT32  x,
  INT32  y
  )
{
  pm_metal_gfx_surface_t  *surf;
  INT32                    row;
  INT32                    col;

  surf = pm_metal_gfx_surface ();
  if (surf == NULL || surf->pixels == NULL) {
    return;
  }

  for (row = 0; row < UI_CUR_H; row++) {
    for (col = 0; col < UI_CUR_W; col++) {
      UINT8                 cell;
      pm_metal_gfx_color_t  c;
      INT32                 px;
      INT32                 py;

      cell = mCurMask[row][col];
      if (cell == 0) {
        continue;
      }

      c  = (cell == 1)
             ? PM_METAL_GFX_RGB (0x00, 0x00, 0x00)
             : PM_METAL_GFX_RGB (0xff, 0xff, 0xff);
      px = x + col;
      py = y + row;
      if (px < 0 || py < 0
          || (UINT32)px >= surf->width
          || (UINT32)py >= surf->height)
      {
        continue;
      }

      surf->pixels[(UINT32)py * surf->pitch + (UINT32)px] = c;
    }
  }
}

void
pm_metal_ui_cursor_invalidate (
  void
  )
{
  mCurLive = 0;
}

void
pm_metal_ui_cursor_hide (
  void
  )
{
  INT32  ox;
  INT32  oy;
  INT32  ow;
  INT32  oh;

  if (!mCurLive) {
    return;
  }

  MetalUiCursorBounds (mCurX, mCurY, &ox, &oy, &ow, &oh);
  MetalUiCursorRestoreUnder ();
  mCurLive = 0;
  if (ow > 0 && oh > 0) {
    (VOID)pm_metal_gfx_present_rect (ox, oy, ow, oh);
  }
}

void
pm_metal_ui_cursor_paint (
  int32_t  x,
  int32_t  y
  )
{
  /* Shadow FB already holds chrome; capture under then stamp. No present. */
  mCurLive = 0;
  MetalUiCursorSaveUnder (x, y);
  MetalUiCursorBlit (x, y);
  mCurX    = x;
  mCurY    = y;
  mCurLive = 1;
}

void
pm_metal_ui_cursor_move (
  int32_t  x,
  int32_t  y
  )
{
  INT32  ox0;
  INT32  oy0;
  INT32  ow0;
  INT32  oh0;
  INT32  ox1;
  INT32  oy1;
  INT32  ow1;
  INT32  oh1;
  INT32  ux;
  INT32  uy;
  INT32  uw;
  INT32  uh;

  if (mCurLive && mCurX == x && mCurY == y) {
    return;
  }

  ox0 = oy0 = ow0 = oh0 = 0;
  if (mCurLive) {
    MetalUiCursorBounds (mCurX, mCurY, &ox0, &oy0, &ow0, &oh0);
    MetalUiCursorRestoreUnder ();
    mCurLive = 0;
  }

  MetalUiCursorSaveUnder (x, y);
  MetalUiCursorBlit (x, y);
  mCurX    = x;
  mCurY    = y;
  mCurLive = 1;
  MetalUiCursorBounds (x, y, &ox1, &oy1, &ow1, &oh1);

  if (ow0 <= 0 || oh0 <= 0) {
    if (ow1 > 0 && oh1 > 0) {
      (VOID)pm_metal_gfx_present_rect (ox1, oy1, ow1, oh1);
    }

    return;
  }

  if (ow1 <= 0 || oh1 <= 0) {
    (VOID)pm_metal_gfx_present_rect (ox0, oy0, ow0, oh0);
    return;
  }

  ux = (ox0 < ox1) ? ox0 : ox1;
  uy = (oy0 < oy1) ? oy0 : oy1;
  uw = ((ox0 + ow0) > (ox1 + ow1) ? (ox0 + ow0) : (ox1 + ow1)) - ux;
  uh = ((oy0 + oh0) > (oy1 + oh1) ? (oy0 + oh0) : (oy1 + oh1)) - uy;
  (VOID)pm_metal_gfx_present_rect (ux, uy, uw, uh);
}
