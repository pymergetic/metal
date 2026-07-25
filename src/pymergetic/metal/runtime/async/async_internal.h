/* Private shared state for async sources — not for guests or other packages. */
#ifndef METAL_RUNTIME_ASYNC_INTERNAL_H_
#define METAL_RUNTIME_ASYNC_INTERNAL_H_

#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/coro/coro.h>
#include <runtime/task/task.h>

#include <stdint.h>

#include "wasm_export.h"

#ifndef PM_METAL_ASYNC_MAX_HANDLES
#define PM_METAL_ASYNC_MAX_HANDLES 64u
#endif

#ifndef PM_METAL_ASYNC_ROOT_STATE
#define PM_METAL_ASYNC_ROOT_STATE 1024u
#endif

typedef enum {
  PM_METAL_ASYNC_SLOT_FREE = 0,
  PM_METAL_ASYNC_SLOT_GUEST_CORO,
  PM_METAL_ASYNC_SLOT_HOST_CORO,
  PM_METAL_ASYNC_SLOT_HOST_FIBER,
  PM_METAL_ASYNC_SLOT_TASK
} pm_metal_async_slot_kind_t;

/* Per call-in wasm affinity (stamped onto each guest coro by value). */
typedef struct pm_metal_async_callin {
  wasm_module_inst_t   inst;
  wasm_exec_env_t      exec_env;
  wasm_function_inst_t step_fn;
} pm_metal_async_callin_t;

typedef struct {
  pm_metal_coro_t         coro;
  uint32_t                self_h;
  void                   *host_state;  /* Metal TLSF — durable frame */
  uint32_t                guest_state; /* step-scoped linear alias; 0 parked */
  uint32_t                state_bytes;
  pm_metal_async_callin_t callin; /* by value; trampoline uses &callin */
} pm_metal_guest_coro_t;

typedef struct {
  pm_metal_coro_t                   coro;
  pm_metal_async_step_fn_t          step;
  pm_metal_async_state_release_fn_t state_release;
  void                             *state;
  uint32_t                          self_h;
  uint32_t                          state_bytes;
} pm_metal_host_fiber_t;

/* Decoded snapshot of one handle-table slot — see MetalAsyncSlotPeek. The
 * table itself (async.c) is not exposed as a plain array anymore: each
 * slot is a single lock-free tagged word, so any direct field access
 * outside async.c would either race or need its own atomic load. */
typedef struct {
  pm_metal_async_slot_kind_t kind;
  void                      *ptr;
} pm_metal_async_slot_t;

/* Live call-in — owned by async_session.c (one process at a time for now). */
extern pm_metal_async_callin_t mCallin;
extern wasm_module_inst_t      mInst; /* alias: mCallin.inst while live */
extern wasm_exec_env_t         mExecEnv;
extern wasm_function_inst_t    mStepFn;
extern pm_metal_async_handle_t mRootCoroH;
extern pm_metal_async_handle_t mRootTaskH;
extern int32_t                 mActive;
extern unsigned                mSessionCpu;

/* Perf counters — owned by async_session.c */
extern uint64_t mPerfWinStartUs;
extern uint64_t mPerfLastStepEndUs;
extern uint32_t mPerfSteps;
extern uint64_t mPerfStepUsSum;
extern uint64_t mPerfGapUsSum;
extern uint64_t mPerfBlitUsSum;
extern uint64_t mPerfPresentUsSum;
extern uint32_t mPerfPresentFrames;
extern uint64_t mPerfSleepUsSum;
extern uint32_t mPerfSleepCount;
extern uint64_t mPerfPumpUsSum;
extern uint32_t mPerfPumps;
/*
 * Worst-case (not average) within the open window — averages hide rare
 * multi-ms spikes (e.g. QEMU/VNC framebuffer present cost) that stall
 * whichever runner is stepping the guest call-in.
 */
extern uint64_t mPerfStepUsMax;
extern uint64_t mPerfGapUsMax;
extern uint64_t mPerfPresentUsMax;
/* CPU that actually ran the most recent present job (offload target, or
   mSessionCpu when run inline) — see pm_metal_async_perf_note_present_cpu. */
extern uint32_t mPerfPresentCpu;
extern uint32_t mPerfPresentOffloads;

pm_metal_async_handle_t MetalAsyncAlloc(pm_metal_async_slot_kind_t kind, void *ptr);
void                    MetalAsyncClear(pm_metal_async_handle_t h);
void                   *MetalAsyncGet(pm_metal_async_handle_t h, pm_metal_async_slot_kind_t kind);
pm_metal_coro_t        *MetalAsyncGetCoro(pm_metal_async_handle_t h);

/**
 * Decoded snapshot of slot `i` (1..PM_METAL_ASYNC_MAX_HANDLES) — one
 * atomic load + unpack, for callers that need to scan the whole table
 * (session teardown, handle-of-coro lookup) rather than probe a single
 * known handle. Out-of-range `i` returns FREE/NULL.
 */
pm_metal_async_slot_t MetalAsyncSlotPeek(uint32_t i);
void                  MetalAsyncPerfReset(uint64_t now_us);
void                  MetalAsyncPerfMaybeReport(uint64_t now_us);

#endif /* METAL_RUNTIME_ASYNC_INTERNAL_H_ */
