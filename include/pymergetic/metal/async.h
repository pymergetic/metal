/*
 * Metal async — guest/host dual ABI (handle-based; real resume).
 *
 * Guests export pm_metal_guest_step(self_h) -> status (stackless).
 * Host wraps each fiber as a coro trampoline that call_wasm's that export.
 * await parks into the Metal task/runloop; timers/wakes resume the guest.
 */
#ifndef PYMERGETIC_METAL_ASYNC_H_
#define PYMERGETIC_METAL_ASYNC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pm_metal_async_handle_t;

#define PM_METAL_ASYNC_HANDLE_INVALID 0u

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"

typedef enum {
	PM_METAL_PENDING = 0,
	PM_METAL_WAITING,
	PM_METAL_DONE,
	PM_METAL_CANCELLED,
	PM_METAL_ERROR
} pm_metal_status_t;
#else
#include <coro/coro.h> /* pm_metal_status_t — same numeric values */
#endif

#define PM_METAL_ASYNC_WASI_MODULE "pymergetic.metal.async"

#if defined(__wasm__)
#define PM_METAL_ASYNC_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_ASYNC_WASI_MODULE, name)
#endif

#if defined(__wasm__)
/** Alloc guest linear-memory state + host trampoline coro. */
extern pm_metal_async_handle_t pm_metal_async_coro_create(uint32_t state_bytes)
	PM_METAL_ASYNC_IMPORT(pm_metal_async_coro_create);
/** Guest pointer (linear offset) for coro state; 0 if none. */
extern uint32_t pm_metal_async_coro_state(pm_metal_async_handle_t h)
	PM_METAL_ASYNC_IMPORT(pm_metal_async_coro_state);
extern void pm_metal_async_coro_close(pm_metal_async_handle_t h)
	PM_METAL_ASYNC_IMPORT(pm_metal_async_coro_close);

extern pm_metal_async_handle_t pm_metal_async_sleep(uint32_t ms)
	PM_METAL_ASYNC_IMPORT(pm_metal_async_sleep);
extern pm_metal_async_handle_t pm_metal_async_yield(void)
	PM_METAL_ASYNC_IMPORT(pm_metal_async_yield);

/** Wire self→aw; returns WAITING. */
extern int32_t pm_metal_async_await(pm_metal_async_handle_t self_h,
				    pm_metal_async_handle_t aw_h)
	PM_METAL_ASYNC_IMPORT(pm_metal_async_await);

extern pm_metal_async_handle_t
pm_metal_async_create_task(pm_metal_async_handle_t coro_h)
	PM_METAL_ASYNC_IMPORT(pm_metal_async_create_task);
extern int32_t pm_metal_async_await_task(pm_metal_async_handle_t self_h,
					 pm_metal_async_handle_t task_h)
	PM_METAL_ASYNC_IMPORT(pm_metal_async_await_task);
extern void pm_metal_async_task_cancel(pm_metal_async_handle_t task_h)
	PM_METAL_ASYNC_IMPORT(pm_metal_async_task_cancel);
extern int32_t pm_metal_async_task_status(pm_metal_async_handle_t task_h)
	PM_METAL_ASYNC_IMPORT(pm_metal_async_task_status);

extern uint64_t pm_metal_async_mono_ms(void)
	PM_METAL_ASYNC_IMPORT(pm_metal_async_mono_ms);
#else
pm_metal_async_handle_t pm_metal_async_coro_create(uint32_t state_bytes);
uint32_t pm_metal_async_coro_state(pm_metal_async_handle_t h);
void pm_metal_async_coro_close(pm_metal_async_handle_t h);
pm_metal_async_handle_t pm_metal_async_sleep(uint32_t ms);
pm_metal_async_handle_t pm_metal_async_yield(void);
int32_t pm_metal_async_await(pm_metal_async_handle_t self_h,
			     pm_metal_async_handle_t aw_h);
pm_metal_async_handle_t pm_metal_async_create_task(pm_metal_async_handle_t coro_h);
int32_t pm_metal_async_await_task(pm_metal_async_handle_t self_h,
				  pm_metal_async_handle_t task_h);
void pm_metal_async_task_cancel(pm_metal_async_handle_t task_h);
int32_t pm_metal_async_task_status(pm_metal_async_handle_t task_h);
uint64_t pm_metal_async_mono_ms(void);

int pm_metal_async_native_register(void);

/**
 * Bind the current wasm instance for trampoline calls.
 * step_fn must be pm_metal_guest_step(i32)->i32.
 */
int pm_metal_async_session_begin(void *module_inst, void *exec_env,
				 void *step_fn);
/** Spawn root guest coro+task on CPU0. Returns root handle or 0. */
pm_metal_async_handle_t pm_metal_async_session_spawn_root(void);
/** Poll timers + drain CPU0 inbox once. */
void pm_metal_async_session_pump(void);
/** Non-zero if root task has reached a terminal status. */
int pm_metal_async_session_root_done(void);
/** Root status (DONE/ERROR/…). */
int32_t pm_metal_async_session_root_status(void);
void pm_metal_async_session_end(void);
int pm_metal_async_session_active(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_ASYNC_H_ */
