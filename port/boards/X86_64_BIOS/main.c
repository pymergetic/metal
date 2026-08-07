/* Freestanding BIOS entry — console → floor → wamr → µPy. */
#include <stdint.h>

#include "io.h"
#include "main_upy.h"
#include "console_smoke.h"
#include "floor_smoke.h"
#include "wamr_smoke.h"

void uart_init(void);
void uart_puts(const char *s);

#ifndef METAL_UPY_SMOKE
#define METAL_UPY_SMOKE 1
#endif

void pm_metal_bios_main(uint32_t magic, void *mb_info)
{
    (void)magic;
    (void)mb_info;

    uart_init();
    uart_puts("metal X86_64_BIOS\n");

    if (pm_metal_console_smoke() != 0) {
        outw(0x501u, 1u);
        for (;;) {
            __asm__ volatile("hlt");
        }
    }

    if (pm_metal_floor_smoke() != 0) {
        outw(0x501u, 1u);
        for (;;) {
            __asm__ volatile("hlt");
        }
    }

#if defined(METAL_LINK_WAMR) && METAL_LINK_WAMR
    if (pm_metal_wamr_smoke() != 0) {
        outw(0x501u, 1u);
        for (;;) {
            __asm__ volatile("hlt");
        }
    }
#endif

    mp_metal_upy_run(METAL_UPY_SMOKE);

    outw(0x501u, 0u);

    for (;;) {
        __asm__ volatile("hlt");
    }
}
