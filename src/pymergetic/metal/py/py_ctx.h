/** @file
  Per-task MicroPython VM context (opt-in isolation) — the per-CPU
  indirection patched into external/micropython/py/mpstate.h (see
  patches/micropython/0001-metal-percpu-state-ctx.patch) turns
  `mp_state_ctx` from one process-global struct into "whichever context
  is currently claimed by *this* CPU". This file owns that claim: create
  a private heap+state pair for a task, enter/leave it around a single
  synchronous bytecode call (never across an await — see py.c), and
  destroy it on task teardown.

  Host-only, never guest-visible: py.h/py.c are the dual-ABI surface;
  this is pure plumbing behind them. Concerns purposely kept next to
  where mp_state_ctx itself is used (py.c's 4 re-anchor call sites), not
  under include/ (nothing outside py/ needs this type).
**/
#ifndef PM_METAL_PY_CTX_H_
#define PM_METAL_PY_CTX_H_

#include <stddef.h>

#include "py/mpstate.h"

typedef struct pm_metal_py_ctx {
  mp_state_ctx_t *state; /* own gc/vm/thread state, one per isolated task */
  void           *blob;  /* MAP-carved gc heap arena, own arena */
  size_t          blob_bytes;
} pm_metal_py_ctx_t;

/**
 * Size the per-CPU claim table to pm_metal_mem_n_cpus() and point every
 * slot at &mp_state_ctx_default (today's single shared context) — must
 * run once, before the first MP_STATE_.../mp_state_ctx touch of any kind
 * (i.e. before the first mp_embed_init). Idempotent.
 */
void pm_metal_py_ctx_table_init(void);

/**
 * Carve a fresh MAP heap + mp_state_ctx_t, mp_embed_init() it, install
 * the bind/pmcmd/mod tables against it (cheap — a few dozen
 * mp_store_attr calls, see py_bind.c), and append /mods + /mods/py/stdlib
 * to its own sys.path (mirrors pm_metal_py_init() on the shared
 * context — mp_sys_path is a genuine per-context root pointer, so each
 * isolated context needs its own copy of this call, not just the
 * shared context's). Still skips the shared context's c_py_demo seed
 * on purpose — that's a demo module for the c_py_demo boot proof only,
 * not stdlib access. NULL on failure.
 */
pm_metal_py_ctx_t *pm_metal_py_ctx_create(size_t heap_bytes);

/** Frees the arena + state + ctx. Caller must not still be inside it. */
void pm_metal_py_ctx_destroy(pm_metal_py_ctx_t *ctx);

/**
 * Claim this CPU's table slot for @a ctx (NULL = shared/default context)
 * — bracket every synchronous bytecode entry with enter/leave, exactly
 * where mp_stack_set_top() already re-anchors the GC stack-scan
 * boundary (py_exec_and_maybe_main / py_call_bound / py_resume_coro /
 * pm_metal_py_call). Never call across an await/park.
 */
void pm_metal_py_ctx_enter(pm_metal_py_ctx_t *ctx);

/** Releases this CPU's table slot back to the shared/default context. */
void pm_metal_py_ctx_leave(void);

#endif /* PM_METAL_PY_CTX_H_ */
