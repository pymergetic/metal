/*
 * Metal UI — tab widget (guest/host dual ABI).
 *
 * impl: common — src/pymergetic/metal/shell/ui/tabs.c
 */
#ifndef PYMERGETIC_METAL_SHELL_UI_TAB_H_
#define PYMERGETIC_METAL_SHELL_UI_TAB_H_

#include <stdint.h>

#include "pymergetic/metal/dev/gfx/gfx.h"
#include "pymergetic/metal/shell/ui/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__wasm__)
extern pm_metal_ui_handle_t pm_metal_ui_tab_open(const char *title, int activate)
	PM_METAL_UI_IMPORT(pm_metal_ui_tab_open);
extern int pm_metal_ui_tab_close(pm_metal_ui_handle_t h)
	PM_METAL_UI_IMPORT(pm_metal_ui_tab_close);
extern int pm_metal_ui_tab_activate(pm_metal_ui_handle_t h)
	PM_METAL_UI_IMPORT(pm_metal_ui_tab_activate);
extern unsigned pm_metal_ui_tab_count(void)
	PM_METAL_UI_IMPORT(pm_metal_ui_tab_count);
extern pm_metal_ui_handle_t pm_metal_ui_tab_active(void)
	PM_METAL_UI_IMPORT(pm_metal_ui_tab_active);
extern void pm_metal_ui_tab_puts(pm_metal_ui_handle_t h, const char *line)
	PM_METAL_UI_IMPORT(pm_metal_ui_tab_puts);
/** Content surface for tab (clip/present target); 0 if none. */
extern pm_metal_gfx_surface_h pm_metal_ui_tab_surface(pm_metal_ui_handle_t h)
	PM_METAL_UI_IMPORT(pm_metal_ui_tab_surface);
#else
pm_metal_ui_handle_t pm_metal_ui_tab_open(const char *title, int activate);
int pm_metal_ui_tab_close(pm_metal_ui_handle_t h);
int pm_metal_ui_tab_activate(pm_metal_ui_handle_t h);
unsigned pm_metal_ui_tab_count(void);
pm_metal_ui_handle_t pm_metal_ui_tab_active(void);
void pm_metal_ui_tab_puts(pm_metal_ui_handle_t h, const char *line);
pm_metal_gfx_surface_h pm_metal_ui_tab_surface(pm_metal_ui_handle_t h);

/** Host helper: activate by index (0 = console). */
int pm_metal_ui_tab_activate_index(unsigned index);
unsigned pm_metal_ui_tab_active_index(void);
int pm_metal_ui_tab_close_active(void);
/** Content rect in screen pixels; 0 ok. */
int pm_metal_ui_tab_content_rect(pm_metal_ui_handle_t tab, int32_t *ox,
				 int32_t *oy, int32_t *ow, int32_t *oh);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SHELL_UI_TAB_H_ */
