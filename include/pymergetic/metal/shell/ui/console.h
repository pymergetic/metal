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

#if !defined(__wasm__)
#include "pymergetic/metal/log/log.h"
#endif

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
/** Put a line with a semantic log style (per-line FG in the console). */
void pm_metal_ui_console_puts_styled(pm_metal_log_style_t style,
				     const char *line);
void pm_metal_ui_active_puts(const char *line);
/** Scroll active tab console by delta lines (positive = older history). */
void pm_metal_ui_console_scroll_by(int32_t delta_lines);
/** Page scroll: dir > 0 older, dir < 0 newer (≈ visible rows − 1). */
void pm_metal_ui_console_scroll_page(int32_t dir);
/**
 * Pointer over active console: wheel + scrollbar click/drag.
 * buttons/flags from pm_metal_input_pointer_t. Returns 1 if UI dirty.
 */
int pm_metal_ui_console_pointer(int32_t x, int32_t y, uint32_t buttons,
				int32_t wheel, uint32_t flags);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SHELL_UI_CONSOLE_H_ */
