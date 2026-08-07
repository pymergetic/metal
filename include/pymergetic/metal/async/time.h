#ifndef PM_METAL_ASYNC_TIME_H_
#define PM_METAL_ASYNC_TIME_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void pm_metal_time_init(void);
uint64_t pm_metal_async_mono_us(void);
uint64_t pm_metal_time_mono_us(void);

/* Returns async handle; completion via poll. */
uint32_t pm_metal_async_sleep_us(uint64_t us);
uint32_t pm_metal_async_sleep_until_us(uint64_t deadline_us);
uint32_t pm_metal_async_sleep(uint32_t ms);
uint32_t pm_metal_async_yield(void);

#ifdef __cplusplus
}
#endif

#endif
