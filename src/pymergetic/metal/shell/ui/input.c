/** @file
  UI shell input line + pointer hit-test + software cursor.
**/
#include "priv.h"

#include <pymergetic/metal/shell/lifecycle/lifecycle.h>

#include <Library/BaseMemoryLib.h>

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
pm_metal_ui_pointer_hit (
  int32_t  x,
  int32_t  y
  )
{
  UINT32  i;
  INT32   tx;
  UINT32  fw;

  if (gMetalUiTabs == NULL) {
    return 0;
  }

  if (y < gMetalUiTabs->y || y >= gMetalUiTabs->y + gMetalUiTabs->h
      || x < gMetalUiTabs->x || x >= gMetalUiTabs->x + gMetalUiTabs->w)
  {
    return 0;
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
      metal_ui_widget_t  *hit;

      gMetalUiTabs->u.tabs.active = i;
      hit = gMetalUiTabs->u.tabs.tabs[i];
      if (hit != NULL) {
        pm_metal_lifecycle_set (
          hit->surface != 0 ? hit->surface : PM_METAL_GFX_SURFACE_DEFAULT,
          PM_METAL_LIFE_FOCUSED | PM_METAL_LIFE_VISIBLE
          );
      }

      pm_metal_ui_sync_input_focus ();
      return 1;
    }

    tx += tw + 4;
  }

  return 0;
}

void
pm_metal_ui_cursor_draw (
  int32_t  x,
  int32_t  y
  )
{
  INT32  i;

  /* Simple arrow: vertical stem + tip. */
  for (i = 0; i < 14; i++) {
    pm_metal_gfx_fill_rect (x, y + i, (i < 10) ? 2 : (14 - i), 1,
                            PM_METAL_GFX_RGB (0xff, 0xff, 0xff));
    pm_metal_gfx_fill_rect (x + 1, y + i + 1, 1, 1,
                            PM_METAL_GFX_RGB (0x10, 0x10, 0x10));
  }

  pm_metal_gfx_fill_rect (x, y, 1, 14, PM_METAL_GFX_RGB (0x10, 0x10, 0x10));
}
