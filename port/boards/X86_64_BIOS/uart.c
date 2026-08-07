/* Freestanding COM1 UART (115200 8N1) — no Metal headers. */
#include <stddef.h>
#include <stdint.h>

#include "io.h"

#define COM1_BASE 0x3F8u

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
