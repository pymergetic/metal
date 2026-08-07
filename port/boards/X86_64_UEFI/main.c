#include <Uefi.h>

static inline void outb(unsigned short port, unsigned char val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port)
{
    unsigned char val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* COM1 so qemu -serial file: catches the banner without GOP. */
static void com1_init(void)
{
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x01);
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);
}

static void com1_putc(char c)
{
    unsigned spins = 0;
    while ((inb(0x3FD) & 0x20) == 0 && spins++ < 100000) {
    }
    outb(0x3F8, (unsigned char)c);
}

static void com1_puts(const char *s)
{
    for (; s && *s; s++) {
        if (*s == '\n') {
            com1_putc('\r');
        }
        com1_putc(*s);
    }
}

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    (void)ImageHandle;
    com1_init();
    com1_puts("metalmod X86_64_UEFI\n");
    com1_puts("ovmf ok — ports/metal BOARD=X86_64_UEFI\n");

    if (SystemTable != NULL && SystemTable->ConOut != NULL) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut,
            L"metalmod X86_64_UEFI\r\n");
        SystemTable->ConOut->OutputString(SystemTable->ConOut,
            L"ovmf ok — ports/metal BOARD=X86_64_UEFI\r\n");
    }
    if (SystemTable != NULL && SystemTable->BootServices != NULL) {
        SystemTable->BootServices->Stall(200000);
    }
    return EFI_SUCCESS;
}
