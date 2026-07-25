/** @file Builtin metal + metal.aio — mono_us / sleep_us / yield_. */
#include <pymergetic/metal/runtime/async/async.h>

#include "py/mpstate.h"
#include "py/obj.h"

#include "py_internal.h"

typedef struct {
  mp_obj_base_t           base;
  pm_metal_async_handle_t h;
  int                     armed;
  int                     done;
} metal_aw_obj_t;

static mp_obj_t metal_aw_iternext(mp_obj_t self_in)
{
  metal_aw_obj_t    *self = MP_OBJ_TO_PTR(self_in);
  pm_metal_py_job_t *job  = pm_metal_py_job_current();

  if (self->done) {
    return MP_OBJ_STOP_ITERATION;
  }
  if (!self->armed) {
    self->armed = 1;
    if (job != NULL) {
      pm_metal_py_job_set_pending(job, self->h, self_in);
    }
    return self_in;
  }
  self->done = 1;
  return MP_OBJ_STOP_ITERATION;
}

static MP_DEFINE_CONST_OBJ_TYPE(
  metal_aw_type, MP_QSTR_object, MP_TYPE_FLAG_ITER_IS_ITERNEXT, iter, metal_aw_iternext);

mp_obj_t pm_metal_py_new_awaitable(pm_metal_async_handle_t h)
{
  metal_aw_obj_t *o = m_new_obj(metal_aw_obj_t);
  o->base.type      = &metal_aw_type;
  o->h              = h;
  o->armed          = 0;
  o->done           = 0;
  return MP_OBJ_FROM_PTR(o);
}

static mp_obj_t metal_mono_us(void)
{
  return mp_obj_new_int_from_uint(pm_metal_async_mono_us());
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_mono_us_obj, metal_mono_us);

static mp_obj_t metal_sleep_us(mp_obj_t us_obj)
{
  uint64_t us = (uint64_t)mp_obj_get_int(us_obj);
  return pm_metal_py_new_awaitable(pm_metal_async_sleep_us(us));
}
static MP_DEFINE_CONST_FUN_OBJ_1(metal_sleep_us_obj, metal_sleep_us);

static mp_obj_t metal_yield_(void)
{
  return pm_metal_py_new_awaitable(pm_metal_async_yield());
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_yield_obj, metal_yield_);

void pm_metal_py_aio_mod_init(void)
{
  mp_obj_t aio_globals;
  mp_obj_t aio_mod;
  mp_obj_t metal_globals;
  mp_obj_t metal_mod;
  mp_obj_t loaded = MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_loaded_modules_dict));

  /*
	 * Module name is metal.aio — not metal.async.
	 * ``async`` is a keyword; ``import metal.async`` is a SyntaxError in
	 * CPython and MicroPython (and breaks basedpyright).
	 */
  aio_globals = mp_obj_new_dict(8);
  mp_obj_dict_store(aio_globals,
                    MP_OBJ_NEW_QSTR(qstr_from_str("__name__")),
                    MP_OBJ_NEW_QSTR(qstr_from_str("metal.aio")));
  mp_obj_dict_store(
    aio_globals, MP_OBJ_NEW_QSTR(qstr_from_str("mono_us")), MP_OBJ_FROM_PTR(&metal_mono_us_obj));
  mp_obj_dict_store(
    aio_globals, MP_OBJ_NEW_QSTR(qstr_from_str("sleep_us")), MP_OBJ_FROM_PTR(&metal_sleep_us_obj));
  mp_obj_dict_store(
    aio_globals, MP_OBJ_NEW_QSTR(qstr_from_str("yield_")), MP_OBJ_FROM_PTR(&metal_yield_obj));

  aio_mod = mp_obj_new_module(qstr_from_str("metal.aio"));
  ((mp_obj_module_t *)MP_OBJ_TO_PTR(aio_mod))->globals =
    (mp_obj_dict_t *)MP_OBJ_TO_PTR(aio_globals);

  metal_globals = mp_obj_new_dict(8);
  mp_obj_dict_store(metal_globals,
                    MP_OBJ_NEW_QSTR(qstr_from_str("__name__")),
                    MP_OBJ_NEW_QSTR(qstr_from_str("metal")));
  mp_obj_dict_store(
    metal_globals, MP_OBJ_NEW_QSTR(qstr_from_str("__path__")), mp_obj_new_list(0, NULL));
  mp_obj_dict_store(metal_globals, MP_OBJ_NEW_QSTR(qstr_from_str("aio")), aio_mod);

  metal_mod = mp_obj_new_module(qstr_from_str("metal"));
  ((mp_obj_module_t *)MP_OBJ_TO_PTR(metal_mod))->globals =
    (mp_obj_dict_t *)MP_OBJ_TO_PTR(metal_globals);

  mp_obj_dict_store(loaded, MP_OBJ_NEW_QSTR(qstr_from_str("metal")), metal_mod);
  mp_obj_dict_store(loaded, MP_OBJ_NEW_QSTR(qstr_from_str("metal.aio")), aio_mod);
}
