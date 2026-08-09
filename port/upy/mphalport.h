#ifndef MICROPY_INCLUDED_METAL_PORT_MPHALPORT_H
#define MICROPY_INCLUDED_METAL_PORT_MPHALPORT_H

#include <stddef.h>
#include <stdint.h>

#include "py/mpconfig.h"

/* Board UART (COM1 / UEFI serial) — implemented per board. */
void uart_init(void);
void uart_write(const char *s, size_t n);
void uart_puts(const char *s);
int uart_rx_chr(void);

/* Ctrl-C hook — no IRQ keyboard yet; stub like ports/minimal. */
static inline void mp_hal_set_interrupt_char(char c) {
    (void)c;
}

#endif
