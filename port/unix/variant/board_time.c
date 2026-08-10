/*
 * Unix seat board clock — CLOCK_MONOTONIC_RAW (falls back to MONOTONIC).
 * Advances during long coro steps (unlike freestanding software ticks).
 */
#define _POSIX_C_SOURCE 200809L
#include "pymergetic/metal/async/board_time.h"

#include <time.h>

uint64_t pm_metal_board_mono_us(void)
{
    struct timespec ts;

#if defined(CLOCK_MONOTONIC_RAW)
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0)
#endif
    {
        if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
            return 0;
        }
    }
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

void pm_metal_board_time_advance_us(uint64_t us)
{
    (void)us; /* wall clock — no software advance */
}

void pm_metal_board_sleep_us(uint64_t us)
{
    struct timespec req;

    req.tv_sec = (time_t)(us / 1000000ull);
    req.tv_nsec = (long)((us % 1000000ull) * 1000ull);
    while (nanosleep(&req, &req) != 0) {
        /* EINTR — continue remaining */
    }
}
