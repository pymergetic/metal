/*
 * Metal UI — shared shell input line (guest/host dual ABI).
 *
 * One prompt strip shared across tabs; not a per-tab widget.
 *
 * impl: common — src/pymergetic/metal/shell/ui/input.c
 */
#ifndef PYMERGETIC_METAL_SHELL_UI_INPUT_H_
#define PYMERGETIC_METAL_SHELL_UI_INPUT_H_

#include <stdint.h>

#include "pymergetic/metal/shell/ui/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__wasm__)
extern void pm_metal_ui_input_clear(void)
	PM_METAL_UI_IMPORT(pm_metal_ui_input_clear);
extern int pm_metal_ui_input_append(char ch)
	PM_METAL_UI_IMPORT(pm_metal_ui_input_append);
extern int pm_metal_ui_input_backspace(void)
	PM_METAL_UI_IMPORT(pm_metal_ui_input_backspace);
extern int pm_metal_ui_input_text(char *out, uint32_t cap)
	PM_METAL_UI_IMPORT(pm_metal_ui_input_text);
#else
void pm_metal_ui_input_clear(void);
int pm_metal_ui_input_append(char ch);
int pm_metal_ui_input_backspace(void);
int pm_metal_ui_input_text(char *out, uint32_t cap);

/**
 * Route keyboard to shell vs guest from foreground tab + live session.
 * Call after tab activate/close and when a guest session starts/ends.
 */
void pm_metal_ui_sync_input_focus(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SHELL_UI_INPUT_H_ */
