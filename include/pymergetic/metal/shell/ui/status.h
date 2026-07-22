/*
 * Metal UI — status-bar widget (guest/host dual ABI).
 *
 * Left status text only; clock / net tray are host chrome.
 *
 * impl: common — src/pymergetic/metal/shell/ui/tabs.c (+ paint)
 */
#ifndef PYMERGETIC_METAL_SHELL_UI_STATUS_H_
#define PYMERGETIC_METAL_SHELL_UI_STATUS_H_

#include "pymergetic/metal/shell/ui/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__wasm__)
extern void pm_metal_ui_set_status(const char *text)
	PM_METAL_UI_IMPORT(pm_metal_ui_set_status);
#else
void pm_metal_ui_set_status(const char *text);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SHELL_UI_STATUS_H_ */
