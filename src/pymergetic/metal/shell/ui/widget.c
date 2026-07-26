/** @file
  UI widget tree, handles, console buffer helpers.
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "priv.h"

#include <pymergetic/metal/runtime/mem/mem.h>

metal_ui_widget_t *MetalUiAlloc(metal_ui_kind_t kind)
{
  metal_ui_widget_t *w;

  w = (metal_ui_widget_t *)pm_metal_mem_alloc(sizeof(*w), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (w == NULL) {
    return NULL;
  }

  memset(w, 0, sizeof(*w));
  w->kind = kind;
  return w;
}

void MetalUiAttach(metal_ui_widget_t *parent, metal_ui_widget_t *child)
{
  metal_ui_widget_t *p;

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

void MetalUiDetach(metal_ui_widget_t *parent, metal_ui_widget_t *child)
{
  metal_ui_widget_t *p;
  metal_ui_widget_t *prev;

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

void MetalUiDestroyTree(metal_ui_widget_t *w)
{
  metal_ui_widget_t *c;
  metal_ui_widget_t *n;

  if (w == NULL) {
    return;
  }

  c = w->child;
  while (c != NULL) {
    n = c->next;
    MetalUiDestroyTree(c);
    c = n;
  }

  if (w->kind == METAL_UI_KIND_TAB && w->surface != PM_METAL_GFX_SURFACE_INVALID) {
    pm_metal_gfx_surface_free(w->surface);
    w->surface = PM_METAL_GFX_SURFACE_INVALID;
  }

  pm_metal_mem_free(w);
}

pm_metal_ui_handle_t MetalUiHandleAlloc(metal_ui_widget_t *tab)
{
  uint32_t i;

  if (tab == NULL) {
    return PM_METAL_UI_HANDLE_INVALID;
  }

  for (i = 1; i <= MAX_TABS; i++) {
    if (gMetalUiByHandle[i] == NULL) {
      gMetalUiByHandle[i] = tab;
      tab->handle         = (pm_metal_ui_handle_t)i;
      return tab->handle;
    }
  }

  return PM_METAL_UI_HANDLE_INVALID;
}

void MetalUiHandleFree(pm_metal_ui_handle_t h)
{
  if (h == PM_METAL_UI_HANDLE_INVALID || h > MAX_TABS) {
    return;
  }

  if (gMetalUiByHandle[h] != NULL) {
    gMetalUiByHandle[h]->handle = PM_METAL_UI_HANDLE_INVALID;
    gMetalUiByHandle[h]         = NULL;
  }
}

metal_ui_widget_t *MetalUiTabFromHandle(pm_metal_ui_handle_t h)
{
  if (h == PM_METAL_UI_HANDLE_INVALID || h > MAX_TABS) {
    return NULL;
  }

  return gMetalUiByHandle[h];
}

int32_t MetalUiTabIndex(metal_ui_widget_t *tab)
{
  uint32_t i;

  if (gMetalUiTabs == NULL || tab == NULL) {
    return -1;
  }

  for (i = 0; i < gMetalUiTabs->u.tabs.n; i++) {
    if (gMetalUiTabs->u.tabs.tabs[i] == tab) {
      return (int32_t)i;
    }
  }

  return -1;
}

int32_t MetalUiTabIndexAt(int32_t x, int32_t y)
{
  uint32_t i;
  int32_t  tx;
  uint32_t fw;

  if (gMetalUiTabs == NULL) {
    return -1;
  }

  if (y < gMetalUiTabs->y || y >= gMetalUiTabs->y + gMetalUiTabs->h || x < gMetalUiTabs->x ||
      x >= gMetalUiTabs->x + gMetalUiTabs->w) {
    return -1;
  }

  fw = pm_metal_gfx_font_width();
  tx = gMetalUiTabs->x + 2;
  for (i = 0; i < gMetalUiTabs->u.tabs.n; i++) {
    metal_ui_widget_t *tab;
    int32_t            tw;
    uint32_t           tlen;

    tab = gMetalUiTabs->u.tabs.tabs[i];
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

    if (tx + tw > gMetalUiTabs->x + gMetalUiTabs->w - 4) {
      break;
    }

    if (x >= tx && x < tx + tw) {
      return (int32_t)i;
    }

    tx += tw + 4;
  }

  return -1;
}

metal_ui_widget_t *MetalUiTabConsole(metal_ui_widget_t *tab)
{
  metal_ui_widget_t *frame;

  if (tab == NULL || tab->child == NULL) {
    return NULL;
  }

  frame = tab->child;
  return frame->child;
}

metal_ui_widget_t *MetalUiActiveConsole(void)
{
  metal_ui_widget_t *tab;

  if (gMetalUiTabs == NULL || gMetalUiTabs->u.tabs.n == 0) {
    return gMetalUiSysConsole;
  }

  tab = gMetalUiTabs->u.tabs.tabs[gMetalUiTabs->u.tabs.active];
  return MetalUiTabConsole(tab);
}

uint32_t MetalUiConsoleVisibleRows(metal_ui_widget_t *con)
{
  uint32_t fh;

  if (con == NULL || con->kind != METAL_UI_KIND_CONSOLE || con->h <= 0) {
    return 0;
  }

  fh = pm_metal_gfx_font_height();
  if (fh == 0) {
    return 0;
  }

  return (uint32_t)con->h / fh;
}

uint32_t MetalUiConsoleTotalRows(metal_ui_widget_t *con)
{
  if (con == NULL || con->kind != METAL_UI_KIND_CONSOLE) {
    return 0;
  }

  return con->u.console.count + MetalUiConsoleLiveInputRows(con);
}

uint32_t MetalUiConsoleMaxOff(metal_ui_widget_t *con)
{
  uint32_t rows;
  uint32_t total;

  if (con == NULL || con->kind != METAL_UI_KIND_CONSOLE) {
    return 0;
  }

  rows  = MetalUiConsoleVisibleRows(con);
  total = MetalUiConsoleTotalRows(con);
  if (total <= rows) {
    return 0;
  }

  return total - rows;
}

void MetalUiConsoleClampView(metal_ui_widget_t *con)
{
  uint32_t max_off;

  if (con == NULL || con->kind != METAL_UI_KIND_CONSOLE) {
    return;
  }

  max_off = MetalUiConsoleMaxOff(con);
  if (con->u.console.view_off > max_off) {
    con->u.console.view_off = max_off;
  }
}

void MetalUiConsoleScrollTo(metal_ui_widget_t *con, uint32_t view_off)
{
  if (con == NULL || con->kind != METAL_UI_KIND_CONSOLE) {
    return;
  }

  con->u.console.view_off = view_off;
  MetalUiConsoleClampView(con);
}

void MetalUiConsoleScrollBy(metal_ui_widget_t *con, int32_t delta_lines)
{
  int32_t off;

  if (con == NULL || con->kind != METAL_UI_KIND_CONSOLE || delta_lines == 0) {
    return;
  }

  off = (int32_t)con->u.console.view_off + delta_lines;
  if (off < 0) {
    off = 0;
  }

  MetalUiConsoleScrollTo(con, (uint32_t)off);
}

int32_t MetalUiConsoleScrollBarGeom(metal_ui_widget_t *con,
                                    int32_t           *track_x,
                                    int32_t           *track_y,
                                    int32_t           *track_w,
                                    int32_t           *track_h,
                                    int32_t           *thumb_y,
                                    int32_t           *thumb_h)
{
  uint32_t rows;
  uint32_t max_off;
  uint32_t count;
  int32_t  th;
  int32_t  ty;
  int32_t  travel;

  if (con == NULL || con->kind != METAL_UI_KIND_CONSOLE || con->w < UI_SCROLL_W + 8 ||
      con->h <= 0) {
    return 0;
  }

  rows    = MetalUiConsoleVisibleRows(con);
  count   = MetalUiConsoleTotalRows(con);
  max_off = MetalUiConsoleMaxOff(con);
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

  th = (int32_t)((uint64_t)con->h * (uint64_t)rows / (uint64_t)count);
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
  ty = con->y + travel -
       (int32_t)((uint64_t)travel * (uint64_t)con->u.console.view_off / (uint64_t)max_off);
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

void MetalUiConsolePutsStyled(metal_ui_widget_t *con, const char *line, pm_metal_log_style_t style)
{
  uint32_t i;
  uint32_t n;

  if (con == NULL || line == NULL || con->kind != METAL_UI_KIND_CONSOLE) {
    return;
  }

  n = 0;
  while (line[n] != '\0' && n < CONSOLE_COLS - 1) {
    n++;
  }

  i = con->u.console.head;
  memset(con->u.console.lines[i], 0, CONSOLE_COLS);
  memcpy(con->u.console.lines[i], line, n);
  con->u.console.lines[i][n] = '\0';
  con->u.console.styles[i]   = (uint8_t)style;
  con->u.console.head        = (i + 1u) % CONSOLE_LINES;
  if (con->u.console.count < CONSOLE_LINES) {
    con->u.console.count++;
  }

  /* Stick-to-bottom stays; scrolled-up views clamp if history wraps. */
  MetalUiConsoleClampView(con);
}

void MetalUiConsolePuts(metal_ui_widget_t *con, const char *line)
{
  MetalUiConsolePutsStyled(con, line, PM_METAL_LOG_STYLE_DEFAULT);
}

metal_ui_widget_t *MetalUiMakeTabBody(const char *title, int32_t closable, int32_t show_input)
{
  metal_ui_widget_t *tab;
  metal_ui_widget_t *frame;
  metal_ui_widget_t *con;

  tab   = MetalUiAlloc(METAL_UI_KIND_TAB);
  frame = MetalUiAlloc(METAL_UI_KIND_FRAME);
  con   = MetalUiAlloc(METAL_UI_KIND_CONSOLE);
  if (tab == NULL || frame == NULL || con == NULL) {
    MetalUiDestroyTree(tab);
    MetalUiDestroyTree(frame);
    MetalUiDestroyTree(con);
    return NULL;
  }

  snprintf(tab->title, sizeof(tab->title), "%s", title);
  tab->closable             = closable;
  tab->surface              = pm_metal_gfx_surface_alloc();
  con->u.console.show_input = show_input;
  con->u.console.cursor_on  = 1;
  MetalUiAttach(tab, frame);
  MetalUiAttach(frame, con);
  return tab;
}
