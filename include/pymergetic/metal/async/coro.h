#ifndef PM_METAL_ASYNC_CORO_H_
#define PM_METAL_ASYNC_CORO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Step entry: return DONE/ERROR/CANCELLED, PENDING (reschedule), or WAITING. */
typedef uint32_t (*pm_metal_async_step_fn_t)(uint32_t self_h);

/** Create a coro with a zeroed durable frame of `state_bytes` (0 = no frame). */
uint32_t pm_metal_async_coro_create(pm_metal_async_step_fn_t step, uint32_t state_bytes);

/** Pointer to the durable step frame (NULL if none / bad handle). */
void *pm_metal_async_coro_state(uint32_t h);

/** Grow/replace durable frame to at least `n` bytes; NULL on failure. */
void *pm_metal_async_coro_alloc(uint32_t h, uint32_t n);

/** Release handle + frame. Do not use `h` afterward. */
void pm_metal_async_coro_close(uint32_t h);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_ASYNC_CORO_H_ */
