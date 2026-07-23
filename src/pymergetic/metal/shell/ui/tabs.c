/** @file
  UI tabs, console text, status text, focus routing.
**/
#include "priv.h"

#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/guest/wasm/wasm.h>
#include <pymergetic/metal/shell/lifecycle/lifecycle.h>

#include <Library/BaseLib.h>

pm_metal_ui_handle_t
pm_metal_ui_console_handle (
  void
  )
{
  return gMetalUiConsoleHandle;
}

void
pm_metal_ui_sync_input_focus (
  void
  )
{
  metal_ui_widget_t      *tab;
  pm_metal_input_focus_t  want;

  /*
   * Live wasm session owns HID while its stdout tab is foreground:
   *   run <mod>  → console tab + fullscreen FB → guest keys
   *   tab <mod>  → app tab + tab surface       → guest keys
   * Switch away (use 0 / close) → shell again.
   */
  want = PM_METAL_INPUT_FOCUS_SHELL;
  if (pm_metal_wasm_session_active () && gMetalUiTabs != NULL
      && gMetalUiTabs->u.tabs.n > 0
      && gMetalUiTabs->u.tabs.active < gMetalUiTabs->u.tabs.n)
  {
    tab = gMetalUiTabs->u.tabs.tabs[gMetalUiTabs->u.tabs.active];
    if (tab != NULL
        && tab->handle == pm_metal_wasm_stdout_tab ())
    {
      want = PM_METAL_INPUT_FOCUS_GUEST;
    }
  }

  pm_metal_input_set_focus (want);
}

pm_metal_ui_handle_t
pm_metal_ui_tab_open (
  const char  *title,
  int          activate
  )
{
  metal_ui_widget_t     *tab;
  pm_metal_ui_handle_t   h;

  if (gMetalUiTabs == NULL || title == NULL || gMetalUiTabs->u.tabs.n >= MAX_TABS) {
    return PM_METAL_UI_HANDLE_INVALID;
  }

  tab = MetalUiMakeTabBody (title, 1, 0);
  if (tab == NULL) {
    return PM_METAL_UI_HANDLE_INVALID;
  }

  h = MetalUiHandleAlloc (tab);
  if (h == PM_METAL_UI_HANDLE_INVALID) {
    MetalUiDestroyTree (tab);
    return PM_METAL_UI_HANDLE_INVALID;
  }

  gMetalUiTabs->u.tabs.tabs[gMetalUiTabs->u.tabs.n] = tab;
  if (activate) {
    gMetalUiTabs->u.tabs.active = gMetalUiTabs->u.tabs.n;
    pm_metal_lifecycle_set (
      tab->surface != 0 ? tab->surface : PM_METAL_GFX_SURFACE_DEFAULT,
      PM_METAL_LIFE_FOCUSED | PM_METAL_LIFE_VISIBLE
      );
  }

  gMetalUiTabs->u.tabs.n++;
  MetalUiAttach (gMetalUiTabs, tab);
  if (activate) {
    pm_metal_ui_sync_input_focus ();
  }

  return h;
}

int
pm_metal_ui_tab_close (
  pm_metal_ui_handle_t  h
  )
{
  metal_ui_widget_t  *tab;
  UINT32              i;
  UINT32              j;
  UINT32              was;

  tab = MetalUiTabFromHandle (h);
  if (gMetalUiTabs == NULL || tab == NULL || !tab->closable) {
    return -1;
  }

  for (i = 0; i < gMetalUiTabs->u.tabs.n; i++) {
    if (gMetalUiTabs->u.tabs.tabs[i] != tab) {
      continue;
    }

    was = gMetalUiTabs->u.tabs.active;
    if (was == i) {
      pm_metal_lifecycle_blur ();
    }

    MetalUiHandleFree (h);
    MetalUiDetach (gMetalUiTabs, tab);
    MetalUiDestroyTree (tab);
    for (j = i; j + 1 < gMetalUiTabs->u.tabs.n; j++) {
      gMetalUiTabs->u.tabs.tabs[j] = gMetalUiTabs->u.tabs.tabs[j + 1];
    }

    gMetalUiTabs->u.tabs.n--;
    gMetalUiTabs->u.tabs.tabs[gMetalUiTabs->u.tabs.n] = NULL;

    if (gMetalUiTabs->u.tabs.n == 0) {
      gMetalUiTabs->u.tabs.active = 0;
    } else if (was == i) {
      /* Closed the focused tab — land on console. */
      gMetalUiTabs->u.tabs.active = 0;
      {
        metal_ui_widget_t  *next;

        next = gMetalUiTabs->u.tabs.tabs[0];
        if (next != NULL) {
          pm_metal_lifecycle_set (
            next->surface != 0 ? next->surface : PM_METAL_GFX_SURFACE_DEFAULT,
            PM_METAL_LIFE_FOCUSED | PM_METAL_LIFE_VISIBLE
            );
        }
      }
    } else if (was > i) {
      gMetalUiTabs->u.tabs.active = was - 1;
    }

    if (gMetalUiTabs->u.tabs.hover == (INT32)i) {
      gMetalUiTabs->u.tabs.hover = -1;
    } else if (gMetalUiTabs->u.tabs.hover > (INT32)i) {
      gMetalUiTabs->u.tabs.hover--;
    }

    pm_metal_ui_sync_input_focus ();
    return 0;
  }

  return -1;
}

int
pm_metal_ui_tab_activate (
  pm_metal_ui_handle_t  h
  )
{
  metal_ui_widget_t  *tab;
  INT32               idx;

  tab = MetalUiTabFromHandle (h);
  idx = MetalUiTabIndex (tab);
  if (idx < 0) {
    return -1;
  }

  gMetalUiTabs->u.tabs.active = (UINT32)idx;
  pm_metal_lifecycle_set (
    tab->surface != 0 ? tab->surface : PM_METAL_GFX_SURFACE_DEFAULT,
    PM_METAL_LIFE_FOCUSED | PM_METAL_LIFE_VISIBLE
    );
  pm_metal_ui_sync_input_focus ();
  return 0;
}

unsigned
pm_metal_ui_tab_count (
  void
  )
{
  return (gMetalUiTabs != NULL) ? gMetalUiTabs->u.tabs.n : 0;
}

pm_metal_ui_handle_t
pm_metal_ui_tab_active (
  void
  )
{
  metal_ui_widget_t  *tab;

  if (gMetalUiTabs == NULL || gMetalUiTabs->u.tabs.n == 0) {
    return PM_METAL_UI_HANDLE_INVALID;
  }

  tab = gMetalUiTabs->u.tabs.tabs[gMetalUiTabs->u.tabs.active];
  if (tab == NULL) {
    return PM_METAL_UI_HANDLE_INVALID;
  }

  return tab->handle;
}

void
pm_metal_ui_tab_puts (
  pm_metal_ui_handle_t  h,
  const char           *line
  )
{
  metal_ui_widget_t  *tab;

  tab = MetalUiTabFromHandle (h);
  MetalUiConsolePuts (MetalUiTabConsole (tab), line);
}

void
pm_metal_ui_console_puts (
  const char  *line
  )
{
  MetalUiConsolePuts (gMetalUiSysConsole, line);
}

void
pm_metal_ui_console_puts_styled (
  pm_metal_log_style_t  style,
  const char           *line
  )
{
  MetalUiConsolePutsStyled (gMetalUiSysConsole, line, style);
}

void
pm_metal_ui_active_puts (
  const char  *line
  )
{
  MetalUiConsolePuts (MetalUiActiveConsole (), line);
}

void
pm_metal_ui_set_status (
  const char  *text
  )
{
  if (gMetalUiStatus == NULL || text == NULL) {
    return;
  }

  AsciiStrCpyS (gMetalUiStatus->u.status.text, sizeof (gMetalUiStatus->u.status.text), text);
}

int
pm_metal_ui_tab_activate_index (
  unsigned  index
  )
{
  metal_ui_widget_t  *tab;

  if (gMetalUiTabs == NULL || index >= gMetalUiTabs->u.tabs.n) {
    return -1;
  }

  gMetalUiTabs->u.tabs.active = index;
  tab = gMetalUiTabs->u.tabs.tabs[index];
  if (tab != NULL) {
    pm_metal_lifecycle_set (
      tab->surface != 0 ? tab->surface : PM_METAL_GFX_SURFACE_DEFAULT,
      PM_METAL_LIFE_FOCUSED | PM_METAL_LIFE_VISIBLE
      );
  }

  pm_metal_ui_sync_input_focus ();
  return 0;
}

int
pm_metal_ui_tab_cycle (
  int  delta
  )
{
  UINT32  n;
  UINT32  a;
  INT32   next;

  n = pm_metal_ui_tab_count ();
  if (n == 0) {
    return -1;
  }

  a    = pm_metal_ui_tab_active_index ();
  next = (INT32)a + delta;
  while (next < 0) {
    next += (INT32)n;
  }

  next %= (INT32)n;
  return pm_metal_ui_tab_activate_index ((UINT32)next);
}

unsigned
pm_metal_ui_tab_active_index (
  void
  )
{
  return (gMetalUiTabs != NULL) ? gMetalUiTabs->u.tabs.active : 0;
}

int
pm_metal_ui_tab_close_active (
  void
  )
{
  metal_ui_widget_t  *tab;

  if (gMetalUiTabs == NULL || gMetalUiTabs->u.tabs.n == 0) {
    return -1;
  }

  tab = gMetalUiTabs->u.tabs.tabs[gMetalUiTabs->u.tabs.active];
  if (tab == NULL) {
    return -1;
  }

  return pm_metal_ui_tab_close (tab->handle);
}

pm_metal_gfx_surface_h
pm_metal_ui_tab_surface (
  pm_metal_ui_handle_t  h
  )
{
  metal_ui_widget_t  *tab;

  tab = MetalUiTabFromHandle (h);
  if (tab == NULL) {
    return PM_METAL_GFX_SURFACE_INVALID;
  }

  return tab->surface;
}

int
pm_metal_ui_tab_content_rect (
  pm_metal_ui_handle_t  tab_h,
  int32_t              *ox,
  int32_t              *oy,
  int32_t              *ow,
  int32_t              *oh
  )
{
  metal_ui_widget_t  *tab;
  metal_ui_widget_t  *frame;

  tab = MetalUiTabFromHandle (tab_h);
  if (tab == NULL || tab->child == NULL) {
    return -1;
  }

  frame = tab->child;
  if (ox != NULL) {
    *ox = frame->x;
  }

  if (oy != NULL) {
    *oy = frame->y;
  }

  if (ow != NULL) {
    *ow = frame->w;
  }

  if (oh != NULL) {
    *oh = frame->h;
  }

  return 0;
}
