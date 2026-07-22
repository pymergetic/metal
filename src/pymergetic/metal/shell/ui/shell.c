/** @file
  UI shell lifecycle — create/fini/frame/tick.
**/
#include "priv.h"

#include <Library/BaseLib.h>

metal_ui_widget_t    *gMetalUiByHandle[MAX_TABS + 1];
metal_ui_widget_t    *gMetalUiShellRoot;
metal_ui_widget_t    *gMetalUiTabs;
metal_ui_widget_t    *gMetalUiSysConsole;
metal_ui_widget_t    *gMetalUiStatus;
pm_metal_ui_handle_t  gMetalUiConsoleHandle;

int
pm_metal_ui_console_shell (
  void
  )
{
  pm_metal_gfx_surface_t  *surf;
  metal_ui_widget_t       *win;
  metal_ui_widget_t       *tabs;
  metal_ui_widget_t       *tab;
  metal_ui_widget_t       *st;
  pm_metal_ui_handle_t     h;

  if (!pm_metal_gfx_ready ()) {
    return -1;
  }

  surf = pm_metal_gfx_surface ();
  if (surf == NULL) {
    return -1;
  }

  if (gMetalUiShellRoot != NULL) {
    return 0;
  }

  win  = MetalUiAlloc (METAL_UI_KIND_WINDOW);
  tabs = MetalUiAlloc (METAL_UI_KIND_TABS);
  st   = MetalUiAlloc (METAL_UI_KIND_STATUS_BAR);
  tab  = MetalUiMakeTabBody ("console", 0, 1);
  if (win == NULL || tabs == NULL || st == NULL || tab == NULL) {
    MetalUiDestroyTree (win);
    MetalUiDestroyTree (tabs);
    MetalUiDestroyTree (st);
    MetalUiDestroyTree (tab);
    return -1;
  }

  h = MetalUiHandleAlloc (tab);
  if (h == PM_METAL_UI_HANDLE_INVALID) {
    MetalUiDestroyTree (win);
    MetalUiDestroyTree (tabs);
    MetalUiDestroyTree (st);
    MetalUiDestroyTree (tab);
    return -1;
  }

  AsciiStrCpyS (win->title, sizeof (win->title), "Metal - pymergetic");
  AsciiStrCpyS (st->u.status.text, sizeof (st->u.status.text), "ready");

  tabs->u.tabs.tabs[0] = tab;
  tabs->u.tabs.n       = 1;
  tabs->u.tabs.active  = 0;
  MetalUiAttach (tabs, tab);
  MetalUiAttach (win, tabs);
  MetalUiAttach (win, st);

  gMetalUiShellRoot      = win;
  gMetalUiTabs           = tabs;
  gMetalUiSysConsole     = MetalUiTabConsole (tab);
  gMetalUiStatus         = st;
  gMetalUiConsoleHandle  = h; /* first free slot → 1 */

  pm_metal_gfx_clear (COL_DESKTOP);
  MetalUiLayout ();
  MetalUiPaint ();
  (VOID)pm_metal_gfx_present ();
  return 0;
}

void
pm_metal_ui_shutdown (
  void
  )
{
  UINT32  i;

  if (gMetalUiShellRoot != NULL) {
    MetalUiDestroyTree (gMetalUiShellRoot);
  }

  for (i = 0; i <= MAX_TABS; i++) {
    gMetalUiByHandle[i] = NULL;
  }

  gMetalUiShellRoot     = NULL;
  gMetalUiTabs          = NULL;
  gMetalUiSysConsole    = NULL;
  gMetalUiStatus        = NULL;
  gMetalUiConsoleHandle = PM_METAL_UI_HANDLE_INVALID;
}

int
pm_metal_ui_frame (
  void
  )
{
  pm_metal_gfx_surface_h  prev;

  if (gMetalUiShellRoot == NULL) {
    return -1;
  }

  /*
   * Chrome always paints in screen space (DEFAULT). Restore the prior
   * draw surface so windowed guests (tab surface) stay bound afterward.
   */
  prev = pm_metal_gfx_draw_surface ();
  pm_metal_gfx_set_surface (PM_METAL_GFX_SURFACE_DEFAULT);
  MetalUiLayout ();
  MetalUiPaint ();
  pm_metal_gfx_set_surface (prev);
  return 0;
}

int
pm_metal_ui_paint_shell_input (
  void
  )
{
  pm_metal_gfx_surface_h  prev;

  if (gMetalUiShellRoot == NULL || gMetalUiSysConsole == NULL) {
    return -1;
  }

  prev = pm_metal_gfx_draw_surface ();
  MetalUiPaintShellInputLine ();
  pm_metal_gfx_set_surface (prev);
  return 0;
}

int
pm_metal_ui_shell_input_rect (
  int32_t  *x,
  int32_t  *y,
  int32_t  *w,
  int32_t  *h
  )
{
  return MetalUiShellInputGeom (x, y, w, h);
}

int
pm_metal_ui_tick (
  uint64_t  now_ms
  )
{
  INT32  on;
  INT32  changed;

  changed = 0;
  if (gMetalUiSysConsole != NULL) {
    on = ((now_ms / 500u) & 1u) ? 1 : 0;
    if (on != gMetalUiSysConsole->u.console.cursor_on) {
      gMetalUiSysConsole->u.console.cursor_on = on;
    }
  }

  /* Dirty until status paint refreshes its snapshot. */
  if (MetalUiStatusNeedsRefresh ()) {
    changed = 1;
  }

  return changed;
}
