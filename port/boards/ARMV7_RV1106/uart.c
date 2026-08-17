/* RV1106: no UART. Unclocked 0xff4c0000 LSR reads hang the A7. LED is the prove. */
#include <stddef.h>
#include <stdint.h>

void uart_init(void) {
}

void uart_write(const char *s, size_t n) {
    (void)s;
    (void)n;
}

void uart_puts(const char *s) {
    (void)s;
}

int uart_rx_chr(void) {
    return -1;
}
