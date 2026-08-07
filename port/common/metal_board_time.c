/* Freestanding board clock — software µs advanced by delay / poll. */
#include "pymergetic/metal/async/board_time.h"

static uint64_t g_mono_us;

uint64_t pm_metal_board_mono_us(void)
{
    return g_mono_us;
}

void pm_metal_board_time_advance_us(uint64_t us)
{
    g_mono_us += us;
}
