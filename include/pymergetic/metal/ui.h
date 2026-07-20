/*
 * Metal UI — guest/host dual ABI (handle-based; no host pointers to wasm).
 *
 * Handles are opaque uint32_t. 0 is invalid. Console tab is handle 1 after
 * host creates the shell window.
 */
#ifndef PYMERGETIC_METAL_UI_H_
#define PYMERGETIC_METAL_UI_H_

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/wasi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pm_metal_ui_handle_t;

#define PM_METAL_UI_HANDLE_INVALID 0u

#define PM_METAL_UI_WASI_MODULE "pymergetic.metal.ui"

#if defined(__wasm__)
#define PM_METAL_UI_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_UI_WASI_MODULE, name)
#endif

#if !defined(__wasm__)
/** Build Window→Tabs→console shell. Returns 0 ok. */
int pm_metal_ui_console_shell(void);
void pm_metal_ui_fini(void);
int pm_metal_ui_frame(void);
void pm_metal_ui_tick(uint64_t now_ms);
#endif

#if defined(__wasm__)
extern pm_metal_ui_handle_t pm_metal_ui_console_handle(void)
	PM_METAL_UI_IMPORT(pm_metal_ui_console_handle);
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
extern void pm_metal_ui_console_puts(const char *line)
	PM_METAL_UI_IMPORT(pm_metal_ui_console_puts);
extern void pm_metal_ui_active_puts(const char *line)
	PM_METAL_UI_IMPORT(pm_metal_ui_active_puts);
extern void pm_metal_ui_set_status(const char *text)
	PM_METAL_UI_IMPORT(pm_metal_ui_set_status);
extern void pm_metal_ui_input_clear(void)
	PM_METAL_UI_IMPORT(pm_metal_ui_input_clear);
extern int pm_metal_ui_input_append(char ch)
	PM_METAL_UI_IMPORT(pm_metal_ui_input_append);
extern int pm_metal_ui_input_backspace(void)
	PM_METAL_UI_IMPORT(pm_metal_ui_input_backspace);
extern int pm_metal_ui_input_text(char *out, uint32_t cap)
	PM_METAL_UI_IMPORT(pm_metal_ui_input_text);
#else
pm_metal_ui_handle_t pm_metal_ui_console_handle(void);
pm_metal_ui_handle_t pm_metal_ui_tab_open(const char *title, int activate);
int pm_metal_ui_tab_close(pm_metal_ui_handle_t h);
int pm_metal_ui_tab_activate(pm_metal_ui_handle_t h);
unsigned pm_metal_ui_tab_count(void);
pm_metal_ui_handle_t pm_metal_ui_tab_active(void);
void pm_metal_ui_tab_puts(pm_metal_ui_handle_t h, const char *line);
void pm_metal_ui_console_puts(const char *line);
void pm_metal_ui_active_puts(const char *line);
void pm_metal_ui_set_status(const char *text);
void pm_metal_ui_input_clear(void);
int pm_metal_ui_input_append(char ch);
int pm_metal_ui_input_backspace(void);
int pm_metal_ui_input_text(char *out, uint32_t cap);

/** Host helper: activate by index (0 = console). */
int pm_metal_ui_tab_activate_index(unsigned index);
unsigned pm_metal_ui_tab_active_index(void);
int pm_metal_ui_tab_close_active(void);

int pm_metal_ui_native_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_UI_H_ */
