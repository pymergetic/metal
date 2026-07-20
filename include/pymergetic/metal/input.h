/*
 * Metal input — guest/host dual ABI (key ring for games).
 */
#ifndef PYMERGETIC_METAL_INPUT_H_
#define PYMERGETIC_METAL_INPUT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_INPUT_WASI_MODULE "pymergetic.metal.input"

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_INPUT_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_INPUT_WASI_MODULE, name)
#endif

#if defined(__wasm__)
/**
 * Pop one key event. Returns packed (pressed<<8)|key, or 0 if empty.
 */
extern int32_t pm_metal_input_poll_key_packed(void)
	PM_METAL_INPUT_IMPORT(pm_metal_input_poll_key_packed);
#else
int32_t pm_metal_input_poll_key(int32_t *pressed, int32_t *key);
int32_t pm_metal_input_poll_key_packed(void);

/** Low-level enqueue (pressed=0/1). Prefer note_key / set_held from ConIn. */
void pm_metal_input_push_key(int pressed, unsigned char doom_key);

/**
 * ConIn saw doom_key (down or typematic repeat). Emits keydown once, refreshes
 * hold timer; pm_metal_input_tick synthesizes keyup after idle.
 */
void pm_metal_input_note_key(unsigned char doom_key, uint64_t now_ms);

/** Force held/released (modifiers from TextInEx shift state). */
void pm_metal_input_set_held(unsigned char doom_key, int held, uint64_t now_ms);

/** Synthesize keyups for keys idle longer than the hold window. */
void pm_metal_input_tick(uint64_t now_ms);

/** Non-zero while an async game session should own the keyboard. */
int pm_metal_input_game_focus(void);
void pm_metal_input_set_game_focus(int on);

int pm_metal_input_native_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_INPUT_H_ */
