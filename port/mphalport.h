/* Firmware µPy HAL — UART + no timers. */
#ifndef PYMERGETIC_METAL_PORT_MPHALPORT_H
#define PYMERGETIC_METAL_PORT_MPHALPORT_H

#include <stdint.h>

#ifndef MP_INT_TYPE
typedef uintptr_t mp_uint_t;
#endif

static inline __attribute__((unused)) mp_uint_t mp_hal_ticks_ms(void) {
    return 0;
}
static inline __attribute__((unused)) mp_uint_t mp_hal_ticks_us(void) {
    return 0;
}
static inline __attribute__((unused)) void mp_hal_delay_ms(mp_uint_t ms) {
    (void)ms;
}
static inline __attribute__((unused)) void mp_hal_delay_us(mp_uint_t us) {
    (void)us;
}
static inline __attribute__((unused)) void mp_hal_set_interrupt_char(char c) {
    (void)c;
}

#endif
