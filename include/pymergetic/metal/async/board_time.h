#ifndef PM_METAL_BOARD_TIME_H_
#define PM_METAL_BOARD_TIME_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Board / host clocks feed the async floor. Freestanding boards implement
 * this (often software ticks); host smoke uses CLOCK_MONOTONIC. */
uint64_t pm_metal_board_mono_us(void);
void pm_metal_board_time_advance_us(uint64_t us);

#ifdef __cplusplus
}
#endif

#endif
