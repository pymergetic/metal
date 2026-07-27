/*
 * Metal UI — status-bar widget (guest/host dual ABI).
 *
 * Left status text only; host chrome owns clock / net tray / keyb / fps
 * and the audio ready+mute+volume slider (same pm_metal_audio_* API as
 * shell `audio` / pymergetic.metal.audio).
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
extern void pm_metal_ui_set_status(const char *text) PM_METAL_UI_IMPORT(pm_metal_ui_set_status);
#else
void pm_metal_ui_set_status(const char *text);
/** Mute toggle + volume slider on status-bar audio chrome; 1 if handled. */
int pm_metal_ui_status_audio_pointer(int32_t x, int32_t y, uint32_t buttons);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SHELL_UI_STATUS_H_ */
