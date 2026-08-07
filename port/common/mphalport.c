#include <stdint.h>
#include <stddef.h>

#include "py/mphal.h"
#include "py/runtime.h"

#include "mphalport.h"
#include "pymergetic/metal/async/board_time.h"
#include "pymergetic/metal/console.h"

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
    if (pm_metal_console_ready()) {
        return (mp_uint_t)pm_metal_console_write((const uint8_t *)str, len);
    }
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
        pm_metal_board_time_advance_us(1000ull);
    }
}

void mp_hal_delay_us(mp_uint_t us) {
    for (volatile uint32_t s = 0; s < us * 10u; s++) {
    }
    pm_metal_board_time_advance_us((uint64_t)us);
}

mp_uint_t mp_hal_ticks_cpu(void) {
    return s_ticks_ms;
}
