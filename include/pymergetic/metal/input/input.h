/*
 * Metal input — guest/host dual ABI (keys + pointer + lock).
 * See docs/IO.md. Doom scancodes are a host/package translator, not the
 * long-term product key space (Metal keycodes below).
 */
#ifndef PYMERGETIC_METAL_INPUT_H_
#define PYMERGETIC_METAL_INPUT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_INPUT_WASI_MODULE "pymergetic.metal.input"

/* Metal keycodes — USB HID Keyboard/Keypad usage page (subset). */
typedef uint16_t pm_metal_keycode_t;

#define PM_METAL_KEY_NONE     0u
#define PM_METAL_KEY_A        0x04u
#define PM_METAL_KEY_ESCAPE   0x29u
#define PM_METAL_KEY_SPACE    0x2cu
#define PM_METAL_KEY_RIGHT    0x4fu
#define PM_METAL_KEY_LEFT     0x50u
#define PM_METAL_KEY_DOWN     0x51u
#define PM_METAL_KEY_UP       0x52u
#define PM_METAL_KEY_ENTER    0x28u
#define PM_METAL_KEY_LSHIFT   0xe1u
#define PM_METAL_KEY_LALT     0xe2u
#define PM_METAL_KEY_LCTRL    0xe0u

#define PM_METAL_INPUT_PTR_ABSOLUTE  1u
#define PM_METAL_INPUT_PTR_RELATIVE  2u
#define PM_METAL_INPUT_PTR_WHEEL     4u

typedef struct {
	int32_t  x;
	int32_t  y;
	int32_t  dx;
	int32_t  dy;
	uint32_t buttons; /* bit0=L bit1=R bit2=M */
	uint32_t flags;
} pm_metal_input_pointer_t;

typedef struct {
	uint16_t code;    /* pm_metal_keycode_t */
	uint8_t  pressed; /* 0/1 */
	uint8_t  mods;    /* reserved */
} pm_metal_input_key_event_t;

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_INPUT_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_INPUT_WASI_MODULE, name)

/** Transitional: packed (pressed<<8)|doom_key — prefer poll_key_event. */
extern int32_t pm_metal_input_poll_key_packed(void)
	PM_METAL_INPUT_IMPORT(pm_metal_input_poll_key_packed);
/** Pop Metal key event into guest struct at dest; 1=ok, 0=empty. */
extern int32_t pm_metal_input_poll_key_event(uint32_t dest)
	PM_METAL_INPUT_IMPORT(pm_metal_input_poll_key_event);
/** Pop pointer sample into guest struct at dest; 1=ok, 0=empty. */
extern int32_t pm_metal_input_poll_pointer(uint32_t dest)
	PM_METAL_INPUT_IMPORT(pm_metal_input_poll_pointer);
extern int32_t pm_metal_input_pointer_lock(uint32_t surface)
	PM_METAL_INPUT_IMPORT(pm_metal_input_pointer_lock);
extern void pm_metal_input_pointer_unlock(void)
	PM_METAL_INPUT_IMPORT(pm_metal_input_pointer_unlock);
extern int32_t pm_metal_input_pointer_locked(void)
	PM_METAL_INPUT_IMPORT(pm_metal_input_pointer_locked);
#else
int32_t pm_metal_input_poll_key(int32_t *pressed, int32_t *key);
int32_t pm_metal_input_poll_key_packed(void);
int32_t pm_metal_input_poll_key_event(pm_metal_input_key_event_t *out);
int32_t pm_metal_input_poll_pointer(pm_metal_input_pointer_t *out);
int32_t pm_metal_input_pointer_lock(uint32_t surface);
void pm_metal_input_pointer_unlock(void);
int32_t pm_metal_input_pointer_locked(void);

void pm_metal_input_push_key(int pressed, unsigned char doom_key);
void pm_metal_input_note_key(unsigned char doom_key, uint64_t now_ms);
void pm_metal_input_set_held(unsigned char doom_key, int held, uint64_t now_ms);
void pm_metal_input_tick(uint64_t now_ms);

int pm_metal_input_game_focus(void);
void pm_metal_input_set_game_focus(int on);

/** Locate pointer protocols + poll HW into rings (call from shell_poll). */
void pm_metal_input_hw_poll(void);
/** Latest absolute pointer sample (screen coords); buttons bit0=L. */
void pm_metal_input_pointer_sample(int32_t *x, int32_t *y, uint32_t *buttons);
/**
 * Post-EBS keyboard: poll i8042 (QEMU/VNC PS/2) into ASCII.
 * ConIn is dead after ExitBootServices; VNC keys land here.
 */
uint32_t pm_metal_input_ps2_read(char *buf, uint32_t len);

int pm_metal_input_native_register(void);
void pm_metal_input_bind_inst(void *module_inst);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_INPUT_H_ */
