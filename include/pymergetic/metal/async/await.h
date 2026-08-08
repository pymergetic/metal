#ifndef PM_METAL_ASYNC_AWAIT_H_
#define PM_METAL_ASYNC_AWAIT_H_

#include <stdint.h>
#include "pymergetic/metal/async/handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cooperative: park self until child DONE/ERROR (poll loop must run). */
pm_metal_async_status_t pm_metal_async_await(uint32_t self_h, uint32_t child_h);

/* Park until pm_metal_async_wake (net/layer progress). Not a spin. */
uint32_t pm_metal_async_park(void);
void pm_metal_async_wake(uint32_t h);

#ifdef __cplusplus
}
#endif

#endif
