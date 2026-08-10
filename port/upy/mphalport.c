#include <stdint.h>
#include <stddef.h>

#include "py/mphal.h"
#include "py/runtime.h"

#include "mphalport.h"
#include "pymergetic/metal/async/time.h"
#include "pymergetic/metal/console.h"
#include "pymergetic/metal/net/ip/__init__.h"

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
    return (mp_uint_t)pm_metal_time_mono_us() / 1000u;
}

mp_uint_t mp_hal_ticks_us(void) {
    return (mp_uint_t)pm_metal_time_mono_us();
}

void mp_hal_delay_ms(mp_uint_t ms) {
    pm_metal_time_sleep_ms((uint32_t)ms);
    pm_metal_net_ip_poll();
}

void mp_hal_delay_us(mp_uint_t us) {
    pm_metal_time_sleep_us((uint64_t)us);
}

mp_uint_t mp_hal_ticks_cpu(void) {
    return (mp_uint_t)pm_metal_time_mono_us();
}
