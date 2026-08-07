#ifndef PM_METAL_ASYNC_RUNNER_H_
#define PM_METAL_ASYNC_RUNNER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * N CPUs = N equal runners (IO.md). On UP ports n_cpus=1 is fine;
 * the API stays N-runner shaped (round-robin create_task, poll all).
 */
int32_t pm_metal_async_start(uint32_t n_cpus);
int32_t pm_metal_async_ready(void);
uint32_t pm_metal_async_n_runners(void);

/* Enqueue handle on next runner (RR). h from sleep/yield/…; 0 = spawn yield. */
uint32_t pm_metal_async_create_task(uint32_t h);

/* Advance ready work across all runners; returns handles completed. */
int32_t pm_metal_async_run_poll(void);
int32_t pm_metal_async_run_poll_all(void);

/* Poll until idle (nothing WAITING) or no progress. */
int32_t pm_metal_async_run_loop(void);

/* Optional idle pump (e.g. net) invoked once at the start of each run_poll. */
typedef void (*pm_metal_async_idle_pump_fn)(void);
void pm_metal_async_set_idle_pump(pm_metal_async_idle_pump_fn fn);

#ifdef __cplusplus
}
#endif

#endif

