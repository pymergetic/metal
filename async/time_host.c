/* Host CLOCK_MONOTONIC backend for floor smoke. */
#define _POSIX_C_SOURCE 200809L

#include "pymergetic/metal/async/board_time.h"

#include <time.h>

uint64_t pm_metal_board_mono_us(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

void pm_metal_board_time_advance_us(uint64_t us)
{
    (void)us;
}
