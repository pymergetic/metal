/* ARMV7_QEMU — PL011 primecell at 0x09000000 (the virt machine's UART).
 * MMIO only, no clock gating: QEMU's model accepts writes regardless of the
 * programmed baud, so the fixed divisors below are documentation. */
#include <stddef.h>
#include <stdint.h>

#define UART0 ((volatile uint32_t *)0x09000000u)
#define UART_DR   0x00
#define UART_FR   0x18
#define UART_IBRD 0x24
#define UART_FBRD 0x28
#define UART_LCR  0x2c
#define UART_CR   0x30
#define UART_IMSC 0x38
#define UART_ICR  0x44
#define FR_BUSY   (1u << 3)
#define FR_TXFF   (1u << 5)

void uart_init(void) {
    UART0[UART_IBRD >> 2] = 13;      /* 24MHz / (16*115200) ~= 13.02 */
    UART0[UART_FBRD >> 2] = 1;
    UART0[UART_LCR >> 2] = 0x70;     /* 8N1, FIFOs on */
    UART0[UART_ICR >> 2] = 0x7ff;    /* clear all interrupts */
    UART0[UART_IMSC >> 2] = 0;       /* mask all — polled I/O */
    UART0[UART_CR >> 2] = 0x301;     /* UART on, TX on, RX on */
}

static void uart_tx_wait(void) {
    while ((UART0[UART_FR >> 2] & (FR_BUSY | FR_TXFF)) != 0) {
    }
}

void uart_write(const char *s, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        uart_tx_wait();
        UART0[UART_DR >> 2] = (uint32_t)(uint8_t)s[i];
    }
    uart_tx_wait();
}

void uart_puts(const char *s) {
    size_t i = 0;
    while (s[i] != '\0') {
        i++;
    }
    uart_write(s, i);
    uart_write("\r\n", 2);
}

int uart_rx_chr(void) {
    return -1; /* polled transmit only — the prove is one-directional */
}
