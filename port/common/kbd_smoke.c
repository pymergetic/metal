#include "kbd_smoke.h"

#include <stdint.h>

#include "pymergetic/metal/input/kbd.h"
#include "pymergetic/metal/vt.h"

void uart_puts(const char *s);

static int32_t g_last_fn = -1;

static void on_fn(int32_t fn_index, void *user)
{
    (void)user;
    g_last_fn = fn_index;
    if (pm_metal_vt_ready()) {
        (void)pm_metal_vt_switch(fn_index);
    }
}

int pm_metal_kbd_smoke(void)
{
    int32_t i;

    if (pm_metal_kbd_init() != 0) {
        uart_puts("kbd init fail\n");
        return -1;
    }
    pm_metal_kbd_set_fn_callback(on_fn, NULL);

    /* Inject F1..F7 make codes; each should switch VT. */
    for (i = 0; i < 7; i++) {
        g_last_fn = -1;
        pm_metal_kbd_feed_scancode((uint8_t)(PM_METAL_KBD_F1 + (uint8_t)i));
        if (g_last_fn != i) {
            uart_puts("kbd fn fail\n");
            return -1;
        }
        if (pm_metal_vt_ready() && pm_metal_vt_active() != i) {
            uart_puts("kbd vt fail\n");
            return -1;
        }
    }

    /* Break code must not switch. */
    g_last_fn = -1;
    pm_metal_kbd_feed_scancode((uint8_t)(PM_METAL_KBD_F1 | 0x80u));
    if (g_last_fn != -1) {
        uart_puts("kbd break fail\n");
        return -1;
    }

    uart_puts("kbd ok\n");
    return 0;
}
