/*
 * Metal async — guest/host dual ABI (handle-based; real resume).
 *
 * Fiber authors use the same call shape on guest and host:
 *   create(step, state_bytes) / coro_state / sleep / await(self_h, aw) / …
 *
 * Async callables use status(self_h) (stackless). Mod loader does not
 * care about export names — see docs/MODS.md (on_load registers funcs).
 * Host create(step, n) installs a trampoline that calls step(self_h).
 * Guest create uses the current call-in step (session affinity).
 * await parks into the Metal task/runloop; timers/wakes resume the fiber.
 *
 * impl: common —
 *   async.c           handle table + guest/host trampolines
 *   async_ops.c       sleep / present / await / task
 *   async_natives.c   WAMR import table
 *   async_session.c   session + perf window
 */
#ifndef PYMERGETIC_METAL_RUNTIME_ASYNC_ASYNC_H_
#define PYMERGETIC_METAL_RUNTIME_ASYNC_ASYNC_H_

#include <stdint.h>

#include <pymergetic/metal/runtime/mem/mem.h> /* pm_metal_ptr_t */

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
#include <runtime/coro/coro.h> /* pm_metal_status_t — same numeric values */
#endif

/** Fiber step: park via await*, else PENDING / DONE / ERROR / CANCELLED. */
typedef pm_metal_status_t (*pm_metal_async_step_fn_t)(pm_metal_async_handle_t self_h);

#define PM_METAL_ASYNC_WASI_MODULE "pymergetic.metal.async"

#if defined(__wasm__)
#define PM_METAL_ASYNC_IMPORT(name) PM_METAL_WASI_IMPORT(PM_METAL_ASYNC_WASI_MODULE, name)
#endif

#if defined(__wasm__)
/** WASI: state_bytes only; step is the current call-in function. */
extern pm_metal_async_handle_t pm_metal_async_coro_create_wasm(uint32_t state_bytes)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_coro_create);

static inline pm_metal_async_handle_t pm_metal_async_coro_create(pm_metal_async_step_fn_t step,
                                                                 uint32_t state_bytes)
{
  (void)step;
  return pm_metal_async_coro_create_wasm(state_bytes);
}

/**
 * This coro’s step frame (owned by the coro — never mem_free).
 * Guest: linear T* alias for the current step only. Host: native ptr.
 * NULL if none yet.
 */
extern pm_metal_ptr_t pm_metal_async_coro_state(pm_metal_async_handle_t h)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_coro_state);
/**
 * Durable frame on Metal TLSF (same heap as host). Guest: pins a step-scoped
 * linear alias. Freed when the coro is released.
 */
extern pm_metal_ptr_t pm_metal_async_coro_alloc(pm_metal_async_handle_t h, uint32_t n)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_coro_alloc);
/** Get-or-create step frame. Prefer this in steps. */
static inline pm_metal_ptr_t pm_metal_async_coro_frame(pm_metal_async_handle_t h, uint32_t n)
{
  pm_metal_ptr_t p = pm_metal_async_coro_state(h);
  if (p != NULL) {
    return p;
  }
  return pm_metal_async_coro_alloc(h, n);
}
extern void pm_metal_async_coro_close(pm_metal_async_handle_t h)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_coro_close);

extern pm_metal_async_handle_t pm_metal_async_sleep(uint32_t ms)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_sleep);
extern pm_metal_async_handle_t pm_metal_async_sleep_us(uint64_t us)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_sleep_us);
extern pm_metal_async_handle_t pm_metal_async_sleep_until_us(uint64_t deadline_us)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_sleep_until_us);
extern pm_metal_async_handle_t pm_metal_async_yield(void)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_yield);
/** Awaitable present fence for surface (1 = DEFAULT). Chunked LFB + yield. */
extern pm_metal_async_handle_t pm_metal_async_present(uint32_t surface)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_present);
/**
 * Shared 60 Hz draw barrier: await next frame deadline, then present dirty
 * surface if any. Prepare (blit) anytime before this.
 */
extern pm_metal_async_handle_t pm_metal_async_frame(void)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_frame);

/** Wire self→aw; returns WAITING. */
extern pm_metal_status_t pm_metal_async_await(pm_metal_async_handle_t self_h,
                                              pm_metal_async_handle_t aw_h)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_await);

extern pm_metal_async_handle_t pm_metal_async_create_task(pm_metal_async_handle_t coro_h)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_create_task);
extern pm_metal_status_t pm_metal_async_await_task(pm_metal_async_handle_t self_h,
                                                   pm_metal_async_handle_t task_h)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_await_task);
extern void pm_metal_async_task_cancel(pm_metal_async_handle_t task_h)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_task_cancel);
extern pm_metal_status_t pm_metal_async_task_status(pm_metal_async_handle_t task_h)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_task_status);

extern uint64_t pm_metal_async_mono_ms(void) PM_METAL_ASYNC_IMPORT(pm_metal_async_mono_ms);
extern uint64_t pm_metal_async_mono_us(void) PM_METAL_ASYNC_IMPORT(pm_metal_async_mono_us);
/**
 * After await resumes: u32 payload left on self (from the completed child).
 * 0 if none / not a guest coro.
 */
extern uint32_t pm_metal_async_result_u32(pm_metal_async_handle_t self_h)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_result_u32);
/** Leave a u32 on self for the parent after this fiber completes. */
extern void pm_metal_async_set_result_u32(pm_metal_async_handle_t self_h, uint32_t v)
  PM_METAL_ASYNC_IMPORT(pm_metal_async_set_result_u32);
#else
/**
 * Alloc fiber state + trampoline.
 * Host: non-NULL step → host fiber.
 * Guest: NULL step → inherit running guest call-in, else process session.
 */
pm_metal_async_handle_t pm_metal_async_coro_create(pm_metal_async_step_fn_t step,
                                                   uint32_t                 state_bytes);

/**
 * Guest coro stamped with explicit call-in (inst/exec_env/step).
 * No global session required. step_fn is wasm_function_inst_t.
 */
pm_metal_async_handle_t pm_metal_async_coro_create_guest(void    *module_inst,
                                                         void    *exec_env,
                                                         void    *step_fn,
                                                         uint32_t state_bytes);
/** This coro’s frame (owned by coro). Guest: linear alias; host: native. */
pm_metal_ptr_t pm_metal_async_coro_state(pm_metal_async_handle_t h);
/**
 * Durable frame on Metal TLSF. Guest gets a step-scoped linear alias.
 * Host fibers: native heap ptr. Freed on coro release.
 */
pm_metal_ptr_t pm_metal_async_coro_alloc(pm_metal_async_handle_t h, uint32_t n);
/** Get-or-create step frame. Prefer this in steps. */
static inline pm_metal_ptr_t pm_metal_async_coro_frame(pm_metal_async_handle_t h, uint32_t n)
{
  pm_metal_ptr_t p = pm_metal_async_coro_state(h);
  if (p != NULL) {
    return p;
  }
  return pm_metal_async_coro_alloc(h, n);
}
/**
 * Resolve a guest linear buffer for an op that may finish after the step.
 * If guest_off..+len lies in the running coro’s frame alias, returns durable
 * host_state+delta (alias synced into host first). Otherwise returns the
 * linear native pointer (caller must keep that linear alive for the op).
 */
void *pm_metal_async_guest_buf_durable(void *exec_env, uint32_t guest_off, uint32_t len);
void  pm_metal_async_coro_close(pm_metal_async_handle_t h);
pm_metal_async_handle_t pm_metal_async_sleep(uint32_t ms);
pm_metal_async_handle_t pm_metal_async_sleep_us(uint64_t us);
pm_metal_async_handle_t pm_metal_async_sleep_until_us(uint64_t deadline_us);
pm_metal_async_handle_t pm_metal_async_yield(void);
pm_metal_async_handle_t pm_metal_async_present(uint32_t surface);
pm_metal_async_handle_t pm_metal_async_frame(void);
pm_metal_status_t       pm_metal_async_await(pm_metal_async_handle_t self_h,
                                             pm_metal_async_handle_t aw_h);
pm_metal_async_handle_t pm_metal_async_create_task(pm_metal_async_handle_t coro_h);
pm_metal_status_t       pm_metal_async_await_task(pm_metal_async_handle_t self_h,
                                                  pm_metal_async_handle_t task_h);
void                    pm_metal_async_task_cancel(pm_metal_async_handle_t task_h);
pm_metal_status_t       pm_metal_async_task_status(pm_metal_async_handle_t task_h);
/** Stamp process id on an async task (0 = none). */
void     pm_metal_async_task_set_proc_id(pm_metal_async_handle_t task_h, uint32_t proc_id);
uint32_t pm_metal_async_task_proc_id(pm_metal_async_handle_t task_h);
uint64_t pm_metal_async_mono_ms(void);
uint64_t pm_metal_async_mono_us(void);
uint32_t pm_metal_async_result_u32(pm_metal_async_handle_t self_h);
void     pm_metal_async_set_result_u32(pm_metal_async_handle_t self_h, uint32_t v);

/**
 * Optional state teardown for host fibers (create/step). Called with
 * coro_state pointer before the state blob is freed.
 */
typedef void (*pm_metal_async_state_release_fn_t)(void *state);
void pm_metal_async_coro_set_release(pm_metal_async_handle_t           h,
                                     pm_metal_async_state_release_fn_t fn);

/** Host-only: task_run exits when this async task completes. */
void pm_metal_async_task_set_stop_on_done(pm_metal_async_handle_t task_h, int on);

int pm_metal_async_native_register(void);

/**
 * Bind wasm instance + call-in step for trampolines.
 * step_fn is the registered command function (i)i status(self_h).
 */
int pm_metal_async_session_begin(void *module_inst, void *exec_env, void *step_fn);
/** Spawn root coro+task for the session call-in. Returns handle or 0. */
pm_metal_async_handle_t pm_metal_async_session_spawn_root(void);
/**
 * Adopt an existing coro (e.g. from pm_metal_mod_fn_coro) as session root.
 * Returns coro_h or invalid.
 */
pm_metal_async_handle_t pm_metal_async_session_spawn_root_coro(pm_metal_async_handle_t coro_h);
/** Session root task handle (invalid if none). */
pm_metal_async_handle_t pm_metal_async_session_root_task(void);
/** Poll timers + drain session CPU inbox once. */
void pm_metal_async_session_pump(void);
/** Non-zero if root task has reached a terminal status. */
int pm_metal_async_session_root_done(void);
/** Root status (DONE/ERROR/…). */
pm_metal_status_t pm_metal_async_session_root_status(void);
void              pm_metal_async_session_end(void);
int               pm_metal_async_session_active(void);
/** Session runner CPU (only meaningful while session_active). */
unsigned pm_metal_async_session_cpu(void);

/** Host-only: attribute blit/present time to the open perf window (µs). */
void pm_metal_async_perf_note_blit_us(uint64_t us);
void pm_metal_async_perf_note_present_us(uint64_t us);
/** Host-only: one completed present job (frame) in the open perf window. */
void pm_metal_async_perf_note_present_frame(void);
/**
 * Host-only: which CPU actually executed the most-recent present job.
 * `offloaded` != 0 marks it as a cross-runner hand-off (see async_ops.c) —
 * counted separately so metal-perf can show how often offload engaged.
 */
void pm_metal_async_perf_note_present_cpu(unsigned cpu, int offloaded);
/** Host-only: drop the open window (e.g. after wasm startup busy-pump). */
void pm_metal_async_perf_reset(void);
/**
 * Diagnostic A/B switch for the cross-runner present offload (default on).
 * `presentoffload off` forces the legacy inline present path so the two
 * behaviors can be compared without a rebuild. Takes effect on the next
 * present (does not affect one already in flight).
 */
void pm_metal_async_present_offload_set(int on);
int  pm_metal_async_present_offload_get(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_RUNTIME_ASYNC_ASYNC_H_ */
