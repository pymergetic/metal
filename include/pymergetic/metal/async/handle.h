#ifndef PM_METAL_ASYNC_HANDLE_H_
#define PM_METAL_ASYNC_HANDLE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_METAL_ASYNC_PENDING = 0,
    PM_METAL_ASYNC_WAITING = 1,
    PM_METAL_ASYNC_DONE = 2,
    PM_METAL_ASYNC_CANCELLED = 3,
    PM_METAL_ASYNC_ERROR = 4
} pm_metal_async_status_t;

pm_metal_async_status_t pm_metal_async_status(uint32_t h);
void pm_metal_async_set_result_u32(uint32_t h, uint32_t v);
uint32_t pm_metal_async_result_u32(uint32_t h);
/* Sync→async bridge: DONE handle with payload v. */
uint32_t pm_metal_async_completed_u32(uint32_t v);
/* Release a completed/unused handle slot. */
void pm_metal_async_coro_close(uint32_t h);

/** Await-child handle for h, or 0. */
uint32_t pm_metal_async_await_child(uint32_t h);

/** Close h and walk await-child chain (traceless cancel for process quit). */
void pm_metal_async_cancel_tree(uint32_t h);

#ifdef __cplusplus
}
#endif

#endif
