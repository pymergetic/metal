/*
 * Async step meter — optional runtime; compile out with -DPM_METAL_ASYNC_METER=0.
 */
#include "pymergetic/metal/async/meter.h"

#include <string.h>

#ifndef PM_METAL_ASYNC_METER
#define PM_METAL_ASYNC_METER 1
#endif

#if PM_METAL_ASYNC_METER

static volatile int32_t g_on;
static pm_metal_async_meter_snap_t g_snap;

uint64_t pm_metal_async_meter_cycles(void)
{
#if defined(__x86_64__) || defined(__i386__)
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
#else
    /* Coarse fallback — callers on these seats should prefer board mono. */
    extern uint64_t pm_metal_board_mono_us(void);
    return pm_metal_board_mono_us() * 2000ull; /* ~2 GHz nominal */
#endif
}

void pm_metal_async_meter_enable(int32_t on)
{
    g_on = on ? 1 : 0;
}

int32_t pm_metal_async_meter_enabled(void)
{
    return g_on;
}

void pm_metal_async_meter_reset(void)
{
    memset(&g_snap, 0, sizeof(g_snap));
}

void pm_metal_async_meter_snap(pm_metal_async_meter_snap_t *out)
{
    if (out == NULL) {
        return;
    }
    *out = g_snap;
}

/* Called only from async poll when enabled. */
void pm_metal_async_meter_record(uint64_t cycles)
{
    uint32_t b;

    g_snap.steps++;
    g_snap.total_cycles += cycles;
    if (cycles > g_snap.max_cycles) {
        g_snap.max_cycles = cycles;
    }
    if (g_snap.min_cycles == 0ull || cycles < g_snap.min_cycles) {
        g_snap.min_cycles = cycles;
    }
    /* log2 buckets: [0]=0..1, [1]=2..3, … [15]=2^15+ */
    b = 0;
    while (b + 1u < PM_METAL_ASYNC_METER_BUCKETS && (cycles >> (b + 1u)) != 0ull) {
        b++;
    }
    g_snap.buckets[b]++;
}

int32_t pm_metal_async_meter_on_fast(void)
{
    return g_on;
}

#else /* !PM_METAL_ASYNC_METER */

uint64_t pm_metal_async_meter_cycles(void)
{
    return 0;
}
void pm_metal_async_meter_enable(int32_t on)
{
    (void)on;
}
int32_t pm_metal_async_meter_enabled(void)
{
    return 0;
}
void pm_metal_async_meter_reset(void) {}
void pm_metal_async_meter_snap(pm_metal_async_meter_snap_t *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
    }
}
void pm_metal_async_meter_record(uint64_t cycles)
{
    (void)cycles;
}
int32_t pm_metal_async_meter_on_fast(void)
{
    return 0;
}

#endif
