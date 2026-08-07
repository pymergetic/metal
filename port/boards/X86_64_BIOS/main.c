/* Freestanding BIOS entry — COM1 banner + QEMU isa-debug-exit. */
#include <stdint.h>

#include "io.h"

void uart_init(void);
void uart_puts(const char *s);

/* Called from crt0 after long-mode bring-up (Multiboot magic/info unused for MVP). */
void pm_metal_bios_main(uint32_t magic, void *mb_info)
{
    (void)magic;
    (void)mb_info;

    uart_init();
    uart_puts("metalmod X86_64_BIOS\n");
    uart_puts("qemu ok — ports/metal BOARD=X86_64_BIOS\n");

    /* QEMU -device isa-debug-exit,iobase=0x501 — low byte of value becomes (code<<1)|1 exit status. */
    outw(0x501u, 0u);

    for (;;) {
        __asm__ volatile("hlt");
    }
}
