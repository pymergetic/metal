/*
 * Metal UI — umbrella header (guest/host dual ABI).
 *
 * Prefer the per-widget headers when pulling a narrow surface:
 *   types.h / tab.h / console.h / status.h / input.h
 *
 * Handles are opaque uint32_t. 0 is invalid. Console tab is handle 1 after
 * host creates the shell window.
 *
 * impl: common — src/pymergetic/metal/shell/ui/{shell,widget,paint,tabs,input,native}.c
 */
#ifndef PYMERGETIC_METAL_SHELL_UI_UI_H_
#define PYMERGETIC_METAL_SHELL_UI_UI_H_

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/shell/ui/types.h" /* IWYU pragma: export */
#include "pymergetic/metal/shell/ui/tab.h" /* IWYU pragma: export */
#include "pymergetic/metal/shell/ui/console.h" /* IWYU pragma: export */
#include "pymergetic/metal/shell/ui/status.h" /* IWYU pragma: export */
#include "pymergetic/metal/shell/ui/input.h" /* IWYU pragma: export */

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)
/** Build Window→Tabs→console shell. Returns 0 ok. */
int pm_metal_ui_console_shell(void);
void pm_metal_ui_shutdown(void);
/** Layout + paint chrome into the shadow FB (does not present). */
int pm_metal_ui_frame(void);
/** Repaint only the shared shell input line into the shadow FB. */
int pm_metal_ui_paint_shell_input(void);
/** Screen rect of the shell input line; 0 ok. */
int pm_metal_ui_shell_input_rect(int32_t *x, int32_t *y, int32_t *w, int32_t *h);
/** Cursor blink + status tray; returns 1 if status chrome changed. */
int pm_metal_ui_tick(uint64_t now_ms);

/**
 * Hit-test click at screen (x,y). Activates tab under strip; returns 1 if
 * handled, 0 if missed chrome.
 */
int pm_metal_ui_pointer_hit(int32_t x, int32_t y);
/** Draw software cursor at screen position (unlocked pointer). */
void pm_metal_ui_cursor_draw(int32_t x, int32_t y);

int pm_metal_ui_native_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SHELL_UI_UI_H_ */
