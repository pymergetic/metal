/** @file
  Placeholder for historical isolated-context hooks.

  Vanilla MicroPython keeps one process-global `mp_state_ctx`. Metal does
  not patch that, does not own a per-CPU ctx table, and does not carve
  private upy heaps. Python tasks share the embed blob; serialize with
  the run-lock. Alloc is normal Metal mem (`pm_metal_mem_*`).
**/
#ifndef PM_METAL_PY_CTX_H_
#define PM_METAL_PY_CTX_H_

#include <stddef.h>

typedef struct pm_metal_py_ctx {
  void  *unused;
  size_t unused_bytes;
} pm_metal_py_ctx_t;

void               pm_metal_py_ctx_table_init(void);
pm_metal_py_ctx_t *pm_metal_py_ctx_create(size_t heap_bytes);
void               pm_metal_py_ctx_destroy(pm_metal_py_ctx_t *ctx);
void               pm_metal_py_ctx_enter(pm_metal_py_ctx_t *ctx);
void               pm_metal_py_ctx_leave(void);

#endif /* PM_METAL_PY_CTX_H_ */
