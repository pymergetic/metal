#include <stdint.h>

#include "py/mphal.h"
#include "py/runtime.h"

#include "mphalport.h"

/* Rough TSC-free tick: busy-loop calibrated loosely for QEMU smoke only. */
static volatile uint32_t s_ticks_ms;

uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    (void)poll_flags;
    return 0;
}

int mp_hal_stdin_rx_chr(void) {
    return uart_rx_chr();
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    uart_write(str, len);
    return len;
}

mp_uint_t mp_hal_ticks_ms(void) {
    return s_ticks_ms;
}

mp_uint_t mp_hal_ticks_us(void) {
    return s_ticks_ms * 1000u;
}

void mp_hal_delay_ms(mp_uint_t ms) {
    /* Busy wait — replace with PIT/HPET when metal time muscle links in. */
    for (mp_uint_t i = 0; i < ms; i++) {
        for (volatile uint32_t s = 0; s < 50000u; s++) {
        }
        s_ticks_ms++;
    }
}

void mp_hal_delay_us(mp_uint_t us) {
    for (volatile uint32_t s = 0; s < us * 10u; s++) {
    }
}

mp_uint_t mp_hal_ticks_cpu(void) {
    return s_ticks_ms;
}
