/* Freestanding BIOS entry — smoke battery OR lean product REPL. */
#include <stdint.h>

#include "io.h"
#include "main_upy.h"
#include "product_bringup.h"
#include "console_smoke.h"
#include "floor_smoke.h"
#include "net_smoke.h"
#include "ip_smoke.h"
#include "draw_smoke.h"
#include "vt_smoke.h"
#include "tui_smoke.h"
#include "kbd_smoke.h"
#include "wamr_smoke.h"
#include "live_http.h"
#include "live_ssh.h"

void uart_init(void);
void uart_puts(const char *s);

#ifndef METAL_UPY_SMOKE
#define METAL_UPY_SMOKE 1
#endif

#ifndef METAL_LIVE
#define METAL_LIVE 0
#endif

#ifndef METAL_LIVE_SSH
#define METAL_LIVE_SSH 0
#endif

static void bios_halt_fail(void)
{
    outw(0x501u, 1u);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void pm_metal_bios_main(uint32_t magic, void *mb_info)
{
    (void)magic;
    (void)mb_info;

    uart_init();
    uart_puts("metal X86_64_BIOS\n");

#if METAL_UPY_SMOKE
    if (pm_metal_console_smoke() != 0 ||
        pm_metal_floor_smoke() != 0 ||
        pm_metal_net_smoke() != 0 ||
        pm_metal_net_ip_smoke() != 0 ||
        pm_metal_draw_smoke() != 0 ||
        pm_metal_vt_smoke() != 0 ||
        pm_metal_tui_smoke() != 0 ||
        pm_metal_kbd_smoke() != 0) {
        bios_halt_fail();
    }
#if defined(METAL_LINK_WAMR) && METAL_LINK_WAMR
    if (pm_metal_wamr_smoke() != 0) {
        bios_halt_fail();
    }
#endif
#else
    if (pm_metal_product_bringup() != 0) {
        bios_halt_fail();
    }
#endif

    mp_metal_upy_run(METAL_UPY_SMOKE);

#if METAL_LIVE_SSH
    pm_metal_live_ssh();
#elif METAL_LIVE
    pm_metal_live_http();
#else
    outw(0x501u, 0u);
    for (;;) {
        __asm__ volatile("hlt");
    }
#endif
}
