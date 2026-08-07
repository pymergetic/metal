#ifndef PM_METAL_ASYNC_AWAIT_H_
#define PM_METAL_ASYNC_AWAIT_H_

#include <stdint.h>
#include "pymergetic/metal/async/handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cooperative: park self until child DONE/ERROR (poll loop must run). */
pm_metal_async_status_t pm_metal_async_await(uint32_t self_h, uint32_t child_h);

#ifdef __cplusplus
}
#endif

#endif
