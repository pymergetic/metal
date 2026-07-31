/* Private helpers shared by coro sources — not for other packages. */
#ifndef METAL_RUNTIME_CORO_INTERNAL_H_
#define METAL_RUNTIME_CORO_INTERNAL_H_

#include <runtime/coro/coro.h>
#include <runtime/task/task.h>

typedef struct pm_metal_timer pm_metal_timer_t;

void MetalTimerDrop(pm_metal_timer_t **slot);

pm_metal_timer_t *MetalTimerArmAt(uint64_t           deadline_us,
                                  pm_metal_task_t   *task,
                                  pm_metal_coro_t   *wait_for,
                                  pm_metal_timer_t **owner_slot);

pm_metal_timer_t *MetalTimerArm(uint32_t           ms,
                                pm_metal_task_t   *task,
                                pm_metal_coro_t   *wait_for,
                                pm_metal_timer_t **owner_slot);

/**
  Timer fired on a wait_for coro: mark timed_out and optionally retain the
  child task for cancel after the timer lock is dropped.
*/
void MetalWaitForOnTimeout(pm_metal_coro_t *wait_for, pm_metal_task_t **child_out);

#endif /* METAL_RUNTIME_CORO_INTERNAL_H_ */
