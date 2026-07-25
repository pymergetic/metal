/** @file Per-task MicroPython VM context — see py_ctx.h. */
#include <string.h>

#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/runtime/mem/mem.h>

#include "port/micropython_embed.h"
#include "py/mpstate.h"

#include "py_ctx.h"
#include "py_internal.h"

/* Slot count the table was actually sized to (pm_metal_mem_n_cpus() at
 * table-init time) — pm_metal_mem_cpu() must never index past this. */
static uint32_t mCtxTableN;

void pm_metal_py_ctx_table_init(void)
{
  uint32_t n;
  uint32_t i;

  if (mp_metal_py_ctx_table != NULL) {
    return;
  }
  n = pm_metal_mem_n_cpus();
  if (n == 0u) {
    n = 1u;
  }
  mp_metal_py_ctx_table = (mp_state_ctx_t **)pm_metal_mem_alloc(
    (size_t)n * sizeof(mp_state_ctx_t *), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (mp_metal_py_ctx_table == NULL) {
    return;
  }
  for (i = 0; i < n; i++) {
    mp_metal_py_ctx_table[i] = &mp_state_ctx_default;
  }
  mCtxTableN = n;
}

void pm_metal_py_ctx_enter(pm_metal_py_ctx_t *ctx)
{
  uint32_t cpu = pm_metal_mem_cpu();

  if (mp_metal_py_ctx_table == NULL || cpu >= mCtxTableN) {
    return;
  }
  mp_metal_py_ctx_table[cpu] = (ctx != NULL) ? ctx->state : &mp_state_ctx_default;
}

void pm_metal_py_ctx_leave(void)
{
  uint32_t cpu = pm_metal_mem_cpu();

  if (mp_metal_py_ctx_table == NULL || cpu >= mCtxTableN) {
    return;
  }
  mp_metal_py_ctx_table[cpu] = &mp_state_ctx_default;
}

pm_metal_py_ctx_t *pm_metal_py_ctx_create(size_t heap_bytes)
{
  pm_metal_py_ctx_t *ctx;
  int                stack_top;

  ctx =
    (pm_metal_py_ctx_t *)pm_metal_mem_alloc(sizeof(*ctx), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (ctx == NULL) {
    return NULL;
  }
  memset(ctx, 0, sizeof(*ctx));

  ctx->state = (mp_state_ctx_t *)pm_metal_mem_alloc(
    sizeof(mp_state_ctx_t), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (ctx->state == NULL) {
    pm_metal_mem_free(ctx);
    return NULL;
  }
  memset(ctx->state, 0, sizeof(mp_state_ctx_t));

  ctx->blob = pm_metal_mem_map(heap_bytes);
  if (ctx->blob == NULL) {
    pm_metal_mem_free(ctx->state);
    pm_metal_mem_free(ctx);
    return NULL;
  }
  ctx->blob_bytes = heap_bytes;

  /*
   * Claim this CPU's slot for the duration of setup only — mp_embed_init
   * (gc_init + mp_init) and the installers below all resolve MP_STATE_*
   * through whichever context is currently claimed, same as any later
   * bytecode call. stack_top is a throwaway anchor (re-anchored for real
   * at the first actual bytecode entry, same pattern as pm_metal_py_init).
   */
  pm_metal_py_ctx_enter(ctx);
  mp_embed_init(ctx->blob, ctx->blob_bytes, &stack_top);
  pm_metal_py_binds_install();
  pm_metal_py_pmcmd_install();
  pm_metal_py_mod_install();
  /*
   * mp_sys_path (py/runtime.h: MP_STATE_VM(sys_mutable[...])) is a real
   * per-context root pointer, not one of the handful of static-initializer
   * globals patches/micropython/0001-metal-percpu-state-ctx.patch had to
   * pin to the shared context (sys.argv/sys.modules/__main__.dict_main) —
   * mp_embed_init's mp_init() just gave this context a fresh, empty-of-
   * stdlib list. Append the same two entries pm_metal_py_init() puts on
   * the shared context's so this context's own imports can see
   * /mods/py and stdlib.zip too (docs/TODO.md "Isolated-context
   * ergonomics" — this was the missing half).
   */
  pm_metal_py_zip_init_sys_path();
  pm_metal_py_ctx_leave();

  return ctx;
}

void pm_metal_py_ctx_destroy(pm_metal_py_ctx_t *ctx)
{
  if (ctx == NULL) {
    return;
  }
  if (ctx->blob != NULL) {
    pm_metal_mem_unmap(ctx->blob, ctx->blob_bytes);
  }
  if (ctx->state != NULL) {
    pm_metal_mem_free(ctx->state);
  }
  pm_metal_mem_free(ctx);
}
