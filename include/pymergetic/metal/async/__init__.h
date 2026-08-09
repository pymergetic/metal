#ifndef PYMERGETIC_METAL_ASYNC_INIT_H_
#define PYMERGETIC_METAL_ASYNC_INIT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_async_await(uint32_t self_h, uint32_t child_h);
uint32_t pm_metal_async_park(void);
int32_t pm_metal_async_status(void);
int32_t pm_metal_async_start(uint32_t n_cpus);
int32_t pm_metal_async_ready(void);
uint32_t pm_metal_async_n_runners(void);
uint32_t pm_metal_async_create_task(uint32_t h);
int32_t pm_metal_async_run_poll(void);
int32_t pm_metal_async_run_poll_all(void);
int32_t pm_metal_async_run_loop(void);
uint32_t pm_metal_async_sleep_us(uint64_t us);
uint32_t pm_metal_async_yield(void);

#ifdef __cplusplus
}
#endif

#endif
