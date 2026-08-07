#ifndef PM_METAL_ASYNC_RUNNER_H_
#define PM_METAL_ASYNC_RUNNER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_async_start(uint32_t n_cpus);
int32_t pm_metal_async_ready(void);
uint32_t pm_metal_async_n_runners(void);

/* Advance ready work; returns number of handles completed this poll. */
int32_t pm_metal_async_run_poll(void);
int32_t pm_metal_async_run_poll_all(void);

#ifdef __cplusplus
}
#endif

#endif
