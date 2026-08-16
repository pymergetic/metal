/* pymergetic.metal.drivers.rtc — RTC class (unix seconds). */
#ifndef PYMERGETIC_METAL_DRIVERS_RTC_TYPES_H
#define PYMERGETIC_METAL_DRIVERS_RTC_TYPES_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_metal_rtc_ops {
    int64_t (*get)(void *ctx);
    int32_t (*set)(void *ctx, int64_t unix_s);
    void (*close)(void *ctx);
    void *ctx;
} pm_metal_rtc_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DRIVERS_RTC_TYPES_H */
