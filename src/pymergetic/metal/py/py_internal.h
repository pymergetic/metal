#ifndef PM_METAL_PY_INTERNAL_H_
#define PM_METAL_PY_INTERNAL_H_

#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/runtime/async/async.h>

#include "py/obj.h"

#include "py_ctx.h"

typedef struct pm_metal_py_job {
  uint32_t                step;
  uint32_t                after_zip_step; /* target step once PY_STEP_ZIP resolves */
  char                   *src;
  size_t                  src_len;
  mp_obj_t                py_coro; /* async main / call_async coro, or MP_OBJ_NULL */
  mp_obj_t                call_fn; /* bound callable for PY_STEP_CALL */
  uint32_t                call_arg0;
  pm_metal_async_handle_t pending;
  mp_obj_t                pending_aw; /* awaitable obj waiting for pending */
  int                     exe_exception;
  /*
   * NULL (default): shared/default context, mPyRunLock-serialized, exactly
   * today's behavior. Non-NULL: this job exclusively owns an isolated
   * pm_metal_py_ctx_t (own heap, own VM state) for its whole lifetime — no
   * lock needed, and it can run bytecode in true parallel with every other
   * context on a different CPU. See py_ctx.h + docs/MICROPYTHON.md.
   */
  pm_metal_py_ctx_t *ctx;
} pm_metal_py_job_t;

pm_metal_py_job_t *pm_metal_py_job_current(void);
void pm_metal_py_job_set_pending(pm_metal_py_job_t *job, pm_metal_async_handle_t h, mp_obj_t aw);

void pm_metal_py_c_py_demo_seed(void);
void pm_metal_py_pmcmd_install(void);
void pm_metal_py_mod_install(void);

void pm_metal_py_zip_init_sys_path(void);
int  pm_metal_py_zip_ensure(void);
/** Coroutine-friendly zip ensure — see py_zip.c for the 0/-1/1 contract. */
int32_t pm_metal_py_zip_step(pm_metal_async_handle_t self_h, pm_metal_async_handle_t *out_pending);

#endif
