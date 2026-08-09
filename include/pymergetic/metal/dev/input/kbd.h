#ifndef PM_METAL_INPUT_KBD_H_
#define PM_METAL_INPUT_KBD_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Scancode set 1 make codes for F1–F7 (no E0 prefix). */
#define PM_METAL_KBD_F1 0x3Bu
#define PM_METAL_KBD_F2 0x3Cu
#define PM_METAL_KBD_F3 0x3Du
#define PM_METAL_KBD_F4 0x3Eu
#define PM_METAL_KBD_F5 0x3Fu
#define PM_METAL_KBD_F6 0x40u
#define PM_METAL_KBD_F7 0x41u

typedef void (*pm_metal_kbd_fn_cb)(int32_t fn_index, void *user);

int32_t pm_metal_kbd_init(void);
int32_t pm_metal_kbd_ready(void);

/* Register F1–F7 callback (fn_index 0..6). */
void pm_metal_kbd_set_fn_callback(pm_metal_kbd_fn_cb cb, void *user);

/* Feed one set-1 scancode (make or break). Break codes ignored for Fn. */
void pm_metal_kbd_feed_scancode(uint8_t scancode);

/* Poll i8042 status/data ports if present; feed any pending byte. */
void pm_metal_kbd_poll(void);

#ifdef __cplusplus
}
#endif

#endif
