/*
 * Metal input — guest/host dual ABI (keys + pointer + lock).
 * See docs/IO.md.
 *
 * Key space is USB HID Keyboard/Keypad usage IDs (pm_metal_keycode_t).
 * Host drains HW into rings; consumers are focus-routed (shell vs guest).
 *
 * impl: common — src/pymergetic/metal/dev/input/input.c
 * impl: port  — src/{bios,efi}/pymergetic/metal/dev/input/input_port.c
 */
#ifndef PYMERGETIC_METAL_DEV_INPUT_INPUT_H_
#define PYMERGETIC_METAL_DEV_INPUT_INPUT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_INPUT_WASI_MODULE "pymergetic.metal.input"

/* Metal keycodes — USB HID Keyboard/Keypad usage page (subset). */
typedef uint16_t pm_metal_keycode_t;

#define PM_METAL_KEY_NONE     0u
#define PM_METAL_KEY_A        0x04u
#define PM_METAL_KEY_Z        0x1du
#define PM_METAL_KEY_1        0x1eu
#define PM_METAL_KEY_0        0x27u
#define PM_METAL_KEY_ENTER    0x28u
#define PM_METAL_KEY_ESCAPE   0x29u
#define PM_METAL_KEY_BACKSPACE 0x2au
#define PM_METAL_KEY_TAB      0x2bu
#define PM_METAL_KEY_SPACE    0x2cu
#define PM_METAL_KEY_F1       0x3au
#define PM_METAL_KEY_F10      0x43u
#define PM_METAL_KEY_RIGHT    0x4fu
#define PM_METAL_KEY_LEFT     0x50u
#define PM_METAL_KEY_DOWN     0x51u
#define PM_METAL_KEY_UP       0x52u
#define PM_METAL_KEY_LCTRL    0xe0u
#define PM_METAL_KEY_LSHIFT   0xe1u
#define PM_METAL_KEY_LALT     0xe2u
#define PM_METAL_KEY_RCTRL    0xe4u
#define PM_METAL_KEY_RSHIFT   0xe5u
#define PM_METAL_KEY_RALT     0xe6u

#define PM_METAL_INPUT_PTR_ABSOLUTE  1u
#define PM_METAL_INPUT_PTR_RELATIVE  2u
#define PM_METAL_INPUT_PTR_WHEEL     4u

/**
 * Who consumes keyboard/pointer — follows the foreground tab like a
 * normal desktop: console → shell; live guest tab → guest. No app-specific
 * paths; any mod in a tab gets guest focus while it is foreground.
 */
typedef enum {
	PM_METAL_INPUT_FOCUS_SHELL = 0,
	PM_METAL_INPUT_FOCUS_GUEST = 1,
} pm_metal_input_focus_t;

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

/** Pop Metal key event into guest struct at dest; 1=ok, 0=empty. */
extern int32_t pm_metal_input_poll_key_event(uint32_t dest)
	PM_METAL_INPUT_IMPORT(pm_metal_input_poll_key_event);
/** Convenience: pop into guest *pressed / *code linear offsets; prefer poll_key_event. */
extern int32_t pm_metal_input_poll_key(uint32_t pressed_dest, uint32_t code_dest)
	PM_METAL_INPUT_IMPORT(pm_metal_input_poll_key);
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
int32_t pm_metal_input_poll_key(int32_t *pressed, pm_metal_keycode_t *code);
int32_t pm_metal_input_poll_key_event(pm_metal_input_key_event_t *out);
int32_t pm_metal_input_poll_pointer(pm_metal_input_pointer_t *out);
int32_t pm_metal_input_pointer_lock(uint32_t surface);
void pm_metal_input_pointer_unlock(void);
int32_t pm_metal_input_pointer_locked(void);

void pm_metal_input_push_key(int pressed, pm_metal_keycode_t code);
void pm_metal_input_note_key(pm_metal_keycode_t code, uint64_t now_ms);
void pm_metal_input_set_held(pm_metal_keycode_t code, int held, uint64_t now_ms);
void pm_metal_input_tick(uint64_t now_ms);

pm_metal_input_focus_t pm_metal_input_focus(void);
void pm_metal_input_set_focus(pm_metal_input_focus_t focus);

/** Drain port HW into rings (shell_poll / coop pump). */
void pm_metal_input_poll(void);
/** Latest absolute pointer sample (screen coords); buttons bit0=L. */
void pm_metal_input_pointer_sample(int32_t *x, int32_t *y, uint32_t *buttons);
/**
 * Bring up i8042 keyboard (enable + scancode translate). Safe to call often.
 * Real PCs need this after Multiboot/VESA; QEMU often already works.
 * impl: efi — src/efi/pymergetic/metal/dev/input/input_port.c
 */
int pm_metal_input_ps2_init(void);

/**
 * DOS KEYB-style layout for set-1 → shell ASCII (US default, GR = QWERTZ).
 * impl: common — src/pymergetic/metal/dev/input/keyb.c
 */
typedef enum {
	PM_METAL_INPUT_KEYB_US = 0,
	PM_METAL_INPUT_KEYB_GR = 1,
} pm_metal_input_keyb_t;

int pm_metal_input_keyb_set(pm_metal_input_keyb_t layout);
pm_metal_input_keyb_t pm_metal_input_keyb_get(void);
/** "us" / "gr"; NULL if unknown. */
const char *pm_metal_input_keyb_name(pm_metal_input_keyb_t layout);
/**
 * Parse DOS KEYB id ("us", "gr", "de"). Returns 0 and sets *out on ok.
 */
int pm_metal_input_keyb_parse(const char *id, pm_metal_input_keyb_t *out);
/** Map set-1 make (low 7 bits) + shift → CHAR8; 0 = no glyph. */
char pm_metal_input_keyb_ascii(uint8_t set1_make, int shift);
/**
 * Map set-1 make (+ E0 ext) → USB HID usage. Positional (US key places),
 * independent of KEYB ASCII layout — guests keep stable keycodes under
 * `keyb gr`.
 */
pm_metal_keycode_t pm_metal_input_keyb_hid(uint8_t set1_make, int ext);

/** Port backends feed shared rings via these. */
void pm_metal_input_ascii_push(char ch);
void pm_metal_input_pointer_enqueue(const pm_metal_input_pointer_t *ev);
void pm_metal_input_pointer_set_sample(int32_t x, int32_t y, uint32_t buttons);

/**
 * Pop shell ASCII ring (filled by input_poll when focus is shell).
 */
uint32_t pm_metal_input_ps2_read(char *buf, uint32_t len);

int pm_metal_input_native_register(void);
void pm_metal_input_bind_inst(void *module_inst);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_INPUT_INPUT_H_ */
