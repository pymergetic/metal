#ifndef PM_METAL_PY_INTERNAL_H_
#define PM_METAL_PY_INTERNAL_H_

#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/runtime/async/async.h>

#include "py/obj.h"

typedef struct pm_metal_py_job {
  uint32_t                step;
  char                   *src;
  size_t                  src_len;
  mp_obj_t                py_coro; /* async main / call_async coro, or MP_OBJ_NULL */
  mp_obj_t                call_fn; /* bound callable for PY_STEP_CALL */
  uint32_t                call_arg0;
  pm_metal_async_handle_t pending;
  mp_obj_t                pending_aw; /* awaitable obj waiting for pending */
  int                     exe_exception;
} pm_metal_py_job_t;

pm_metal_py_job_t *pm_metal_py_job_current(void);
void pm_metal_py_job_set_pending(pm_metal_py_job_t *job, pm_metal_async_handle_t h, mp_obj_t aw);

void     pm_metal_py_aio_mod_init(void);
void     pm_metal_py_c_py_demo_seed(void);
mp_obj_t pm_metal_py_new_awaitable(pm_metal_async_handle_t h);

void pm_metal_py_zip_init_sys_path(void);
int  pm_metal_py_zip_ensure(void);

#endif
