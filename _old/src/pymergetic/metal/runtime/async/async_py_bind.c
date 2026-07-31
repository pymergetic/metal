/** @file Builtin pymergetic.metal.aio — mono_us / sleep_us / yield_. */
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>
#include <pymergetic/metal/runtime/async/async.h>

/*
 * Only MicroPython header this file needs — MP_DEFINE_CONST_FUN_OBJ_N is
 * the mechanical "this is now a Python-callable" boilerplate every
 * PM_METAL_PY_BIND row requires; everything else (values, the awaitable
 * bridge) goes through py_obj.h instead of py/runtime.h or py_internal.h.
 */
#include "py/obj.h"

static mp_obj_t metal_mono_us(void)
{
  return pm_metal_py_int_new((int64_t)pm_metal_async_mono_us());
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_mono_us_obj, metal_mono_us);
PM_METAL_PY_BIND(
  g_py_bind_aio_mono_us, "pymergetic.metal.aio", "mono_us", metal_mono_us_obj, PM_METAL_PY_SYNC);

static mp_obj_t metal_sleep_us(mp_obj_t us_obj)
{
  uint64_t us = (uint64_t)pm_metal_py_int_get(us_obj);
  return pm_metal_py_new_awaitable(pm_metal_async_sleep_us(us));
}
static MP_DEFINE_CONST_FUN_OBJ_1(metal_sleep_us_obj, metal_sleep_us);
PM_METAL_PY_BIND(
  g_py_bind_aio_sleep_us, "pymergetic.metal.aio", "sleep_us", metal_sleep_us_obj, PM_METAL_PY_SYNC);

static mp_obj_t metal_yield_(void)
{
  return pm_metal_py_new_awaitable(pm_metal_async_yield());
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_yield_obj, metal_yield_);
PM_METAL_PY_BIND(
  g_py_bind_aio_yield, "pymergetic.metal.aio", "yield_", metal_yield_obj, PM_METAL_PY_SYNC);
