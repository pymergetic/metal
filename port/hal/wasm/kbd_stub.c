/*
 * Browser dev.input.kbd — same C ABI; no i8042; feed can invoke Fn callback.
 */
#include "pymergetic/metal/dev/input/kbd.h"

static pm_metal_kbd_fn_cb g_fn_cb;
static void *g_fn_user;
static int32_t g_ready;

int32_t pm_metal_kbd_init(void)
{
    g_ready = 1;
    return 0;
}

int32_t pm_metal_kbd_ready(void) { return g_ready; }

void pm_metal_kbd_set_fn_callback(pm_metal_kbd_fn_cb cb, void *user)
{
    g_fn_cb = cb;
    g_fn_user = user;
}

void pm_metal_kbd_feed_scancode(uint8_t scancode)
{
    int32_t fn;

    /* Break codes (bit7) ignored for Fn. */
    if ((scancode & 0x80u) != 0u) {
        return;
    }
    if (scancode < PM_METAL_KBD_F1 || scancode > PM_METAL_KBD_F7) {
        return;
    }
    fn = (int32_t)(scancode - PM_METAL_KBD_F1);
    if (g_fn_cb) {
        g_fn_cb(fn, g_fn_user);
    }
}

void pm_metal_kbd_poll(void) {}
