/** @file
  pymergetic.metal.time — real-time-clock primitives for Python's own
  time.py (mods/py/stdlib_src/time.py). Backed by the same wall clock
  random.c already exposes to wasm guests (pm_metal_realtime_ms, fed by
  EFI's gRT->GetTime()/BIOS's CMOS RTC, refined by SNTP on success — see
  net/ntp/ntp.c) plus the monotonic TSC clock (runtime/time/time.h). No
  MICROPY_PY_BUILTINS_FLOAT in this build, so every value here is an
  integer (ms/us), never CPython's float seconds — time.py does the
  int-only arithmetic on top.
**/
#include <pymergetic/metal/dev/random/random.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>
#include <runtime/time/time.h>

#include "py/obj.h"

static mp_obj_t metal_time_realtime_ms(void)
{
  return pm_metal_py_int_new((int64_t)pm_metal_realtime_ms());
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_time_realtime_ms_obj, metal_time_realtime_ms);
PM_METAL_PY_BIND(g_py_bind_time_realtime_ms,
                 "pymergetic.metal.time",
                 "realtime_ms",
                 metal_time_realtime_ms_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t metal_time_mono_us(void)
{
  return pm_metal_py_int_new((int64_t)pm_metal_time_mono_us());
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_time_mono_us_obj, metal_time_mono_us);
PM_METAL_PY_BIND(g_py_bind_time_mono_us,
                 "pymergetic.metal.time",
                 "mono_us",
                 metal_time_mono_us_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t metal_time_tz_minutes(void)
{
  return pm_metal_py_int_new((int64_t)pm_metal_tz_minutes());
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_time_tz_minutes_obj, metal_time_tz_minutes);
PM_METAL_PY_BIND(g_py_bind_time_tz_minutes,
                 "pymergetic.metal.time",
                 "tz_minutes",
                 metal_time_tz_minutes_obj,
                 PM_METAL_PY_SYNC);

/** Real busy-stall (TSC), not a coroutine yield — matches CPython's own
 * time.sleep() being a genuine blocking call, not an awaitable. */
static mp_obj_t metal_time_sleep_ms(mp_obj_t ms_obj)
{
  int64_t ms = pm_metal_py_int_get(ms_obj);

  if (ms > 0) {
    pm_metal_time_msleep((uint32_t)ms);
  }

  return pm_metal_py_obj_none();
}
static MP_DEFINE_CONST_FUN_OBJ_1(metal_time_sleep_ms_obj, metal_time_sleep_ms);
PM_METAL_PY_BIND(g_py_bind_time_sleep_ms,
                 "pymergetic.metal.time",
                 "sleep_ms",
                 metal_time_sleep_ms_obj,
                 PM_METAL_PY_SYNC);
