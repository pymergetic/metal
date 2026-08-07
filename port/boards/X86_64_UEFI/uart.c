/* Freestanding COM1 UART for OVMF/QEMU serial — TX + blocking RX. */
#include <stddef.h>
#include <stdint.h>

#define COM1_BASE 0x3F8u

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static int g_ready;

void uart_init(void)
{
    outb(COM1_BASE + 1u, 0x00);
    outb(COM1_BASE + 3u, 0x80);
    outb(COM1_BASE + 0u, 0x01);
    outb(COM1_BASE + 1u, 0x00);
    outb(COM1_BASE + 3u, 0x03);
    outb(COM1_BASE + 2u, 0xC7);
    outb(COM1_BASE + 4u, 0x0B);
    g_ready = 1;
}

static void uart_putc(char c)
{
    uint32_t spins;
    if (!g_ready) {
        uart_init();
    }
    for (spins = 0; spins < 100000u; spins++) {
        if ((inb(COM1_BASE + 5u) & 0x20u) != 0u) {
            break;
        }
    }
    outb(COM1_BASE, (uint8_t)c);
}

void uart_write(const char *s, size_t n)
{
    size_t i;
    if (s == NULL || n == 0) {
        return;
    }
    if (!g_ready) {
        uart_init();
    }
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\n') {
            uart_putc('\r');
        }
        uart_putc(c);
    }
}

void uart_puts(const char *s)
{
    size_t n = 0;
    if (s == NULL) {
        return;
    }
    while (s[n] != '\0') {
        n++;
    }
    uart_write(s, n);
}

int uart_rx_chr(void)
{
    if (!g_ready) {
        uart_init();
    }
    for (;;) {
        if ((inb(COM1_BASE + 5u) & 0x01u) != 0u) {
            return (int)inb(COM1_BASE);
        }
        __asm__ volatile("pause");
    }
}
