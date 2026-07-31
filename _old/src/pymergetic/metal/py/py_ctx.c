/** @file Shared-only MicroPython context — see py_ctx.h. */
#include "py_ctx.h"

void pm_metal_py_ctx_table_init(void) {}

void pm_metal_py_ctx_enter(pm_metal_py_ctx_t *ctx)
{
  (void)ctx;
}

void pm_metal_py_ctx_leave(void) {}

pm_metal_py_ctx_t *pm_metal_py_ctx_create(size_t heap_bytes)
{
  (void)heap_bytes;
  return NULL;
}

void pm_metal_py_ctx_destroy(pm_metal_py_ctx_t *ctx)
{
  (void)ctx;
}
