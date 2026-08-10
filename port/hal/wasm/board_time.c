/*
 * Browser seat board clock — wall µs via Emscripten.
 */
#include "pymergetic/metal/async/board_time.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

uint64_t pm_metal_board_mono_us(void)
{
#if defined(__EMSCRIPTEN__)
    return (uint64_t)(emscripten_get_now() * 1000.0);
#else
    return 0;
#endif
}

void pm_metal_board_time_advance_us(uint64_t us)
{
    (void)us;
}

void pm_metal_board_sleep_us(uint64_t us)
{
#if defined(__EMSCRIPTEN__)
    /* Browser: ms granularity; blocks the worker (ok for seat teardown). */
    double ms = (double)us / 1000.0;
    if (ms < 1.0 && us > 0ull) {
        ms = 1.0;
    }
    emscripten_sleep((unsigned int)ms);
#else
    (void)us;
#endif
}
