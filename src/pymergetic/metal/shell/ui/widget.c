/** @file
  UI widget tree, handles, console buffer helpers.
**/
#include "priv.h"

#include <runtime/mem/mem.h>

#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>

metal_ui_widget_t *
MetalUiAlloc (
  metal_ui_kind_t  kind
  )
{
  metal_ui_widget_t  *w;

  w = (metal_ui_widget_t *)pm_metal_mem_alloc (
                             sizeof (*w),
                             PM_METAL_MEM_HEAP,
                             PM_METAL_MEM_ID_NONE
                             );
  if (w == NULL) {
    return NULL;
  }

  ZeroMem (w, sizeof (*w));
  w->kind = kind;
  return w;
}

VOID
MetalUiAttach (
  metal_ui_widget_t  *parent,
  metal_ui_widget_t  *child
  )
{
  metal_ui_widget_t  *p;

  if (parent == NULL || child == NULL) {
    return;
  }

  child->parent = parent;
  if (parent->child == NULL) {
    parent->child = child;
    return;
  }

  p = parent->child;
  while (p->next != NULL) {
    p = p->next;
  }

  p->next = child;
}

VOID
MetalUiDetach (
  metal_ui_widget_t  *parent,
  metal_ui_widget_t  *child
  )
{
  metal_ui_widget_t  *p;
  metal_ui_widget_t  *prev;

  if (parent == NULL || child == NULL) {
    return;
  }

  prev = NULL;
  p    = parent->child;
  while (p != NULL) {
    if (p == child) {
      if (prev == NULL) {
        parent->child = p->next;
      } else {
        prev->next = p->next;
      }

      child->parent = NULL;
      child->next   = NULL;
      return;
    }

    prev = p;
    p    = p->next;
  }
}

VOID
MetalUiDestroyTree (
  metal_ui_widget_t  *w
  )
{
  metal_ui_widget_t  *c;
  metal_ui_widget_t  *n;

  if (w == NULL) {
    return;
  }

  c = w->child;
  while (c != NULL) {
    n = c->next;
    MetalUiDestroyTree (c);
    c = n;
  }

  if (w->kind == METAL_UI_KIND_TAB && w->surface != PM_METAL_GFX_SURFACE_INVALID) {
    pm_metal_gfx_surface_free (w->surface);
    w->surface = PM_METAL_GFX_SURFACE_INVALID;
  }

  pm_metal_mem_free (w);
}

pm_metal_ui_handle_t
MetalUiHandleAlloc (
  metal_ui_widget_t  *tab
  )
{
  UINT32  i;

  if (tab == NULL) {
    return PM_METAL_UI_HANDLE_INVALID;
  }

  for (i = 1; i <= MAX_TABS; i++) {
    if (gMetalUiByHandle[i] == NULL) {
      gMetalUiByHandle[i] = tab;
      tab->handle  = (pm_metal_ui_handle_t)i;
      return tab->handle;
    }
  }

  return PM_METAL_UI_HANDLE_INVALID;
}

VOID
MetalUiHandleFree (
  pm_metal_ui_handle_t  h
  )
{
  if (h == PM_METAL_UI_HANDLE_INVALID || h > MAX_TABS) {
    return;
  }

  if (gMetalUiByHandle[h] != NULL) {
    gMetalUiByHandle[h]->handle = PM_METAL_UI_HANDLE_INVALID;
    gMetalUiByHandle[h]         = NULL;
  }
}

metal_ui_widget_t *
MetalUiTabFromHandle (
  pm_metal_ui_handle_t  h
  )
{
  if (h == PM_METAL_UI_HANDLE_INVALID || h > MAX_TABS) {
    return NULL;
  }

  return gMetalUiByHandle[h];
}

INT32
MetalUiTabIndex (
  metal_ui_widget_t  *tab
  )
{
  UINT32  i;

  if (gMetalUiTabs == NULL || tab == NULL) {
    return -1;
  }

  for (i = 0; i < gMetalUiTabs->u.tabs.n; i++) {
    if (gMetalUiTabs->u.tabs.tabs[i] == tab) {
      return (INT32)i;
    }
  }

  return -1;
}

INT32
MetalUiTabIndexAt (
  INT32  x,
  INT32  y
  )
{
  UINT32  i;
  INT32   tx;
  UINT32  fw;

  if (gMetalUiTabs == NULL) {
    return -1;
  }

  if (y < gMetalUiTabs->y || y >= gMetalUiTabs->y + gMetalUiTabs->h
      || x < gMetalUiTabs->x || x >= gMetalUiTabs->x + gMetalUiTabs->w)
  {
    return -1;
  }

  fw = pm_metal_gfx_font_width ();
  tx = gMetalUiTabs->x + 2;
  for (i = 0; i < gMetalUiTabs->u.tabs.n; i++) {
    metal_ui_widget_t  *tab;
    INT32               tw;
    UINT32              tlen;

    tab = gMetalUiTabs->u.tabs.tabs[i];
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

    if (tx + tw > gMetalUiTabs->x + gMetalUiTabs->w - 4) {
      break;
    }

    if (x >= tx && x < tx + tw) {
      return (INT32)i;
    }

    tx += tw + 4;
  }

  return -1;
}

metal_ui_widget_t *
MetalUiTabConsole (
  metal_ui_widget_t  *tab
  )
{
  metal_ui_widget_t  *frame;

  if (tab == NULL || tab->child == NULL) {
    return NULL;
  }

  frame = tab->child;
  return frame->child;
}

metal_ui_widget_t *
MetalUiActiveConsole (
  VOID
  )
{
  metal_ui_widget_t  *tab;

  if (gMetalUiTabs == NULL || gMetalUiTabs->u.tabs.n == 0) {
    return gMetalUiSysConsole;
  }

  tab = gMetalUiTabs->u.tabs.tabs[gMetalUiTabs->u.tabs.active];
  return MetalUiTabConsole (tab);
}

UINT32
MetalUiConsoleVisibleRows (
  metal_ui_widget_t  *con
  )
{
  UINT32  fh;

  if (con == NULL || con->kind != METAL_UI_KIND_CONSOLE || con->h <= 0) {
    return 0;
  }

  fh = pm_metal_gfx_font_height ();
  if (fh == 0) {
    return 0;
  }

  return (UINT32)con->h / fh;
}

UINT32
MetalUiConsoleMaxOff (
  metal_ui_widget_t  *con
  )
{
  UINT32  rows;

  if (con == NULL || con->kind != METAL_UI_KIND_CONSOLE) {
    return 0;
  }

  rows = MetalUiConsoleVisibleRows (con);
  if (con->u.console.count <= rows) {
    return 0;
  }

  return con->u.console.count - rows;
}

VOID
MetalUiConsoleClampView (
  metal_ui_widget_t  *con
  )
{
  UINT32  max_off;

  if (con == NULL || con->kind != METAL_UI_KIND_CONSOLE) {
    return;
  }

  max_off = MetalUiConsoleMaxOff (con);
  if (con->u.console.view_off > max_off) {
    con->u.console.view_off = max_off;
  }
}

VOID
MetalUiConsoleScrollTo (
  metal_ui_widget_t  *con,
  UINT32              view_off
  )
{
  if (con == NULL || con->kind != METAL_UI_KIND_CONSOLE) {
    return;
  }

  con->u.console.view_off = view_off;
  MetalUiConsoleClampView (con);
}

VOID
MetalUiConsoleScrollBy (
  metal_ui_widget_t  *con,
  INT32               delta_lines
  )
{
  INT32  off;

  if (con == NULL || con->kind != METAL_UI_KIND_CONSOLE || delta_lines == 0) {
    return;
  }

  off = (INT32)con->u.console.view_off + delta_lines;
  if (off < 0) {
    off = 0;
  }

  MetalUiConsoleScrollTo (con, (UINT32)off);
}

INT32
MetalUiConsoleScrollBarGeom (
  metal_ui_widget_t  *con,
  INT32              *track_x,
  INT32              *track_y,
  INT32              *track_w,
  INT32              *track_h,
  INT32              *thumb_y,
  INT32              *thumb_h
  )
{
  UINT32  rows;
  UINT32  max_off;
  UINT32  count;
  INT32   th;
  INT32   ty;
  INT32   travel;

  if (con == NULL || con->kind != METAL_UI_KIND_CONSOLE
      || con->w < UI_SCROLL_W + 8 || con->h <= 0)
  {
    return 0;
  }

  rows    = MetalUiConsoleVisibleRows (con);
  count   = con->u.console.count;
  max_off = MetalUiConsoleMaxOff (con);
  if (track_x != NULL) {
    *track_x = con->x + con->w - UI_SCROLL_W;
  }

  if (track_y != NULL) {
    *track_y = con->y;
  }

  if (track_w != NULL) {
    *track_w = UI_SCROLL_W;
  }

  if (track_h != NULL) {
    *track_h = con->h;
  }

  if (max_off == 0 || rows == 0 || count == 0) {
    if (thumb_y != NULL) {
      *thumb_y = con->y;
    }

    if (thumb_h != NULL) {
      *thumb_h = 0;
    }

    return 0;
  }

  th = (INT32)((UINT64)con->h * (UINT64)rows / (UINT64)count);
  if (th < 12) {
    th = 12;
  }

  if (th > con->h) {
    th = con->h;
  }

  travel = con->h - th;
  if (travel < 0) {
    travel = 0;
  }

  /* view_off=0 at bottom → thumb at bottom; max_off at top → thumb at top */
  ty = con->y + travel
       - (INT32)((UINT64)travel * (UINT64)con->u.console.view_off
                 / (UINT64)max_off);
  if (ty < con->y) {
    ty = con->y;
  }

  if (ty + th > con->y + con->h) {
    ty = con->y + con->h - th;
  }

  if (thumb_y != NULL) {
    *thumb_y = ty;
  }

  if (thumb_h != NULL) {
    *thumb_h = th;
  }

  return 1;
}

VOID
MetalUiConsolePutsStyled (
  metal_ui_widget_t     *con,
  CONST CHAR8           *line,
  pm_metal_log_style_t   style
  )
{
  UINT32  i;
  UINT32  n;

  if (con == NULL || line == NULL || con->kind != METAL_UI_KIND_CONSOLE) {
    return;
  }

  n = 0;
  while (line[n] != '\0' && n < CONSOLE_COLS - 1) {
    n++;
  }

  i = con->u.console.head;
  ZeroMem (con->u.console.lines[i], CONSOLE_COLS);
  CopyMem (con->u.console.lines[i], line, n);
  con->u.console.lines[i][n] = '\0';
  con->u.console.styles[i]   = (UINT8)style;
  con->u.console.head = (i + 1u) % CONSOLE_LINES;
  if (con->u.console.count < CONSOLE_LINES) {
    con->u.console.count++;
  }

  /* Stick-to-bottom stays; scrolled-up views clamp if history wraps. */
  MetalUiConsoleClampView (con);
}

VOID
MetalUiConsolePuts (
  metal_ui_widget_t  *con,
  CONST CHAR8        *line
  )
{
  MetalUiConsolePutsStyled (con, line, PM_METAL_LOG_STYLE_DEFAULT);
}

metal_ui_widget_t *
MetalUiMakeTabBody (
  CONST CHAR8  *title,
  INT32         closable,
  INT32         show_input
  )
{
  metal_ui_widget_t  *tab;
  metal_ui_widget_t  *frame;
  metal_ui_widget_t  *con;

  tab   = MetalUiAlloc (METAL_UI_KIND_TAB);
  frame = MetalUiAlloc (METAL_UI_KIND_FRAME);
  con   = MetalUiAlloc (METAL_UI_KIND_CONSOLE);
  if (tab == NULL || frame == NULL || con == NULL) {
    MetalUiDestroyTree (tab);
    MetalUiDestroyTree (frame);
    MetalUiDestroyTree (con);
    return NULL;
  }

  AsciiStrCpyS (tab->title, sizeof (tab->title), title);
  tab->closable             = closable;
  tab->surface              = pm_metal_gfx_surface_alloc ();
  con->u.console.show_input = show_input;
  con->u.console.cursor_on  = 1;
  MetalUiAttach (tab, frame);
  MetalUiAttach (frame, con);
  return tab;
}
