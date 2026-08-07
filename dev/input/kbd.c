#include "pymergetic/metal/input/kbd.h"

#include <stddef.h>
#include <stdint.h>

/* Local port I/O so muscle does not depend on port/ path. */
static inline uint8_t kbd_inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

#ifndef PM_METAL_KBD_STATUS
#define PM_METAL_KBD_STATUS 0x64u
#endif
#ifndef PM_METAL_KBD_DATA
#define PM_METAL_KBD_DATA 0x60u
#endif

static int32_t g_ready;
static pm_metal_kbd_fn_cb g_fn_cb;
static void *g_fn_user;

int32_t pm_metal_kbd_init(void)
{
    g_ready = 1;
    g_fn_cb = NULL;
    g_fn_user = NULL;
    return 0;
}

int32_t pm_metal_kbd_ready(void)
{
    return g_ready;
}

void pm_metal_kbd_set_fn_callback(pm_metal_kbd_fn_cb cb, void *user)
{
    g_fn_cb = cb;
    g_fn_user = user;
}

void pm_metal_kbd_feed_scancode(uint8_t scancode)
{
    int32_t fn;

    if (!g_ready) {
        return;
    }
    if (scancode & 0x80u) {
        return;
    }
    if (scancode < PM_METAL_KBD_F1 || scancode > PM_METAL_KBD_F7) {
        return;
    }
    fn = (int32_t)(scancode - PM_METAL_KBD_F1);
    if (g_fn_cb != NULL) {
        g_fn_cb(fn, g_fn_user);
    }
}

void pm_metal_kbd_poll(void)
{
    uint8_t st;

    if (!g_ready) {
        return;
    }
    st = kbd_inb(PM_METAL_KBD_STATUS);
    if ((st & 0x01u) == 0u) {
        return;
    }
    pm_metal_kbd_feed_scancode(kbd_inb(PM_METAL_KBD_DATA));
}
