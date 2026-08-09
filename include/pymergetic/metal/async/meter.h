#ifndef PYMERGETIC_METAL_ASYNC_METER_H_
#define PYMERGETIC_METAL_ASYNC_METER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Step-duration metering — “atomic uninterruptedness”.
 *
 * Compile: PM_METAL_ASYNC_METER=0 strips hooks; default 1 keeps them.
 * Runtime: enable(0) is the fast path (one predicted-false branch).
 *
 * Units: host cycles (RDTSC on x86); not wall µs. Convert with
 * calibrated cycles_per_us when you need SI.
 */

enum { PM_METAL_ASYNC_METER_BUCKETS = 16 };

typedef struct {
    uint64_t steps;
    uint64_t total_cycles;
    uint64_t max_cycles;
    uint64_t min_cycles; /* 0 = none yet */
    uint64_t buckets[PM_METAL_ASYNC_METER_BUCKETS]; /* log2 width */
} pm_metal_async_meter_snap_t;

void pm_metal_async_meter_enable(int32_t on);
int32_t pm_metal_async_meter_enabled(void);
void pm_metal_async_meter_reset(void);
void pm_metal_async_meter_snap(pm_metal_async_meter_snap_t *out);

/* Cheap cycle read for benches (RDTSC / fallback mono*scale). */
uint64_t pm_metal_async_meter_cycles(void);

/* Internal: poll path (predicted-false when disabled). */
int32_t pm_metal_async_meter_on_fast(void);
void pm_metal_async_meter_record(uint64_t cycles);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_ASYNC_METER_H_ */
