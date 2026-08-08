#ifndef PM_METAL_ASYNC_PRIO_H_
#define PM_METAL_ASYNC_PRIO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shared coop classes (not net-local). Net only tags prio on wake.
 * Drain order: due timing → High → Medium → Low, with usage weight
 * so Low is not starved forever.
 */
typedef enum {
    PM_METAL_ASYNC_PRIO_HIGH = 0,
    PM_METAL_ASYNC_PRIO_MED = 1,
    PM_METAL_ASYNC_PRIO_LOW = 2
} pm_metal_async_prio_t;

void pm_metal_async_set_prio(uint32_t h, pm_metal_async_prio_t prio);
pm_metal_async_prio_t pm_metal_async_get_prio(uint32_t h);

/* Enqueue on runner with explicit prio (h=0 → yield). */
uint32_t pm_metal_async_create_task_prio(uint32_t h, uint32_t runner,
                                         pm_metal_async_prio_t prio);

#ifdef __cplusplus
}
#endif

#endif
