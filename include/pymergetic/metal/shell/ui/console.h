/*
 * Metal UI — console widget (guest/host dual ABI).
 *
 * System console tab text sink; active_puts targets the foreground tab.
 *
 * impl: common — src/pymergetic/metal/shell/ui/tabs.c
 */
#ifndef PYMERGETIC_METAL_SHELL_UI_CONSOLE_H_
#define PYMERGETIC_METAL_SHELL_UI_CONSOLE_H_

#include "pymergetic/metal/shell/ui/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__wasm__)
extern pm_metal_ui_handle_t pm_metal_ui_console_handle(void)
	PM_METAL_UI_IMPORT(pm_metal_ui_console_handle);
extern void pm_metal_ui_console_puts(const char *line)
	PM_METAL_UI_IMPORT(pm_metal_ui_console_puts);
extern void pm_metal_ui_active_puts(const char *line)
	PM_METAL_UI_IMPORT(pm_metal_ui_active_puts);
#else
pm_metal_ui_handle_t pm_metal_ui_console_handle(void);
void pm_metal_ui_console_puts(const char *line);
void pm_metal_ui_active_puts(const char *line);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SHELL_UI_CONSOLE_H_ */
