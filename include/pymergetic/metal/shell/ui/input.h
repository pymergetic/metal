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

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__wasm__)
#include "pymergetic/metal/shell/ui/types.h" /* PM_METAL_UI_IMPORT */
extern void pm_metal_ui_input_clear(void) PM_METAL_UI_IMPORT(pm_metal_ui_input_clear);
extern int  pm_metal_ui_input_append(char ch) PM_METAL_UI_IMPORT(pm_metal_ui_input_append);
extern int  pm_metal_ui_input_backspace(void) PM_METAL_UI_IMPORT(pm_metal_ui_input_backspace);
extern int  pm_metal_ui_input_delete_fwd(void) PM_METAL_UI_IMPORT(pm_metal_ui_input_delete_fwd);
extern int  pm_metal_ui_input_text(char *out, uint32_t cap)
  PM_METAL_UI_IMPORT(pm_metal_ui_input_text);
extern int pm_metal_ui_input_set(const char *text) PM_METAL_UI_IMPORT(pm_metal_ui_input_set);
#else
void pm_metal_ui_input_clear(void);
/** Insert at cursor (printable or '\\n'). */
int pm_metal_ui_input_append(char ch);
int pm_metal_ui_input_backspace(void);
/** Remove char at cursor (Delete key); 0 = removed, -1 = at end / no line. */
int pm_metal_ui_input_delete_fwd(void);
int pm_metal_ui_input_text(char *out, uint32_t cap);
/** Replace the shared input (history recall). Truncates to INPUT_CHARS-1. */
int pm_metal_ui_input_set(const char *text);
/** Move insert cursor by delta bytes; clamps. Returns 0. */
int pm_metal_ui_input_move_cursor(int delta);
/**
 * Move by one visual row. Returns 1 if moved, 0 if at buffer edge
 * (caller may history-recall).
 */
int pm_metal_ui_input_move_visual_row(int delta_rows);

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
