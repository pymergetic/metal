#ifndef PM_METAL_ASYNC_TASK_H_
#define PM_METAL_ASYNC_TASK_H_

#include <stdint.h>

#include <pymergetic/metal/async/coro.h>
#include <pymergetic/metal/async/prio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Create coro and schedule it on a runner (RR). */
uint32_t pm_metal_async_spawn(pm_metal_async_step_fn_t step, uint32_t state_bytes,
                              pm_metal_async_prio_t prio);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_ASYNC_TASK_H_ */
