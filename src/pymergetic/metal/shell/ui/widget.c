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

VOID
MetalUiConsolePuts (
  metal_ui_widget_t  *con,
  CONST CHAR8        *line
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
  con->u.console.head = (i + 1u) % CONSOLE_LINES;
  if (con->u.console.count < CONSOLE_LINES) {
    con->u.console.count++;
  }
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
