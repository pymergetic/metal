#ifndef PM_METAL_ASYNC_SMP_H_
#define PM_METAL_ASYNC_SMP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Real multicore: ACPI N, INIT-SIPI (or board hook) APs, each CPU drains
 * only its runner via pm_metal_async_run_poll_cpu / run_loop_cpu.
 * Refuse n < 2 on product paths.
 */

uint32_t pm_metal_smp_cpu_index(void);
uint32_t pm_metal_smp_online_count(void);

/* After async_start(n): bring APs online; each enters run_loop_cpu forever. */
int32_t pm_metal_smp_start(void);

/* Poll / loop one runner (cpu == runner index). */
int32_t pm_metal_async_run_poll_cpu(uint32_t cpu);
int32_t pm_metal_async_run_loop_cpu(uint32_t cpu); /* never returns on AP */

/* Place a handle on a specific runner (not RR). */
uint32_t pm_metal_async_create_task_on(uint32_t h, uint32_t runner);

#ifdef __cplusplus
}
#endif

#endif
