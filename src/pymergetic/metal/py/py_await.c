/** @file
  Metal async handle -> Python awaitable bridge (py_obj.h's
  pm_metal_py_new_awaitable / _u32 / _bytes) — one custom iternext object,
  three constructors. Every C->Python bind that hands back an awaitable
  (pymergetic.metal.aio's sleep_us/yield_, pymergetic.metal.mod's per-
  function calls, ...) goes through this instead of hand-rolling its own
  iternext type, so there is exactly one park/resume implementation.
**/
#include <pymergetic/metal/py/py_obj.h>
#include <pymergetic/metal/runtime/async/async.h>

#include "py/obj.h"
#include "py/runtime.h"

#include "py_internal.h"

typedef struct {
  mp_obj_base_t              base;
  pm_metal_async_handle_t    h;
  int                        armed;
  int                        with_u32_result;
  pm_metal_py_await_bytes_fn bytes_fn;
  void                      *bytes_ctx;
} metal_aw_obj_t;

static mp_obj_t metal_aw_iternext(mp_obj_t self_in)
{
  metal_aw_obj_t    *self = MP_OBJ_TO_PTR(self_in);
  pm_metal_py_job_t *job  = pm_metal_py_job_current();

  if (!self->armed) {
    self->armed = 1;
    if (job != NULL) {
      pm_metal_py_job_set_pending(job, self->h, self_in);
    }
    return self_in;
  }
  if (self->bytes_fn != NULL) {
    const uint8_t *ptr = NULL;
    size_t         len = 0;

    self->bytes_fn(self->bytes_ctx, &ptr, &len);
    return mp_make_stop_iteration(mp_obj_new_bytes(ptr, len));
  }
  if (self->with_u32_result) {
    return mp_make_stop_iteration(mp_obj_new_int_from_uint(pm_metal_async_result_u32(self->h)));
  }
  return MP_OBJ_STOP_ITERATION;
}

static MP_DEFINE_CONST_OBJ_TYPE(
  metal_aw_type, MP_QSTR_object, MP_TYPE_FLAG_ITER_IS_ITERNEXT, iter, metal_aw_iternext);

static pm_metal_py_obj_t metal_aw_new(pm_metal_async_handle_t    h,
                                      int                        with_u32_result,
                                      pm_metal_py_await_bytes_fn bytes_fn,
                                      void                      *bytes_ctx)
{
  metal_aw_obj_t *o  = m_new_obj(metal_aw_obj_t);
  o->base.type       = &metal_aw_type;
  o->h               = h;
  o->armed           = 0;
  o->with_u32_result = with_u32_result;
  o->bytes_fn        = bytes_fn;
  o->bytes_ctx       = bytes_ctx;
  return (pm_metal_py_obj_t)MP_OBJ_FROM_PTR(o);
}

pm_metal_py_obj_t pm_metal_py_new_awaitable(pm_metal_async_handle_t h)
{
  return metal_aw_new(h, 0, NULL, NULL);
}

pm_metal_py_obj_t pm_metal_py_new_awaitable_u32(pm_metal_async_handle_t h)
{
  return metal_aw_new(h, 1, NULL, NULL);
}

pm_metal_py_obj_t pm_metal_py_new_awaitable_bytes(pm_metal_async_handle_t    h,
                                                  pm_metal_py_await_bytes_fn fn,
                                                  void                      *ctx)
{
  return metal_aw_new(h, 0, fn, ctx);
}
