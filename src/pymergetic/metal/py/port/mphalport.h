/* Metal HAL port face — console/time go through Metal; no upy HAL heap. */
#ifndef MICROPY_INCLUDED_METAL_MPHALPORT_H
#define MICROPY_INCLUDED_METAL_MPHALPORT_H

#include <stddef.h>
#include <stdint.h>

void mp_hal_stdout_tx_strn(const char *str, size_t len);
int mp_hal_stdin_rx_chr(void);
uint64_t mp_hal_ticks_us(void);
void mp_hal_delay_us(uint32_t us);

#endif /* MICROPY_INCLUDED_METAL_MPHALPORT_H */
