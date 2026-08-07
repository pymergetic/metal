/* Freestanding BIOS entry — UART + µPy (smoke or REPL). */
#include <stdint.h>

#include "io.h"
#include "main_upy.h"

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

    mp_metal_upy_run(METAL_UPY_SMOKE);

    /* QEMU isa-debug-exit: outw(0x501,0) → exit status 1 (success marker for run). */
    outw(0x501u, 0u);

    for (;;) {
        __asm__ volatile("hlt");
    }
}
