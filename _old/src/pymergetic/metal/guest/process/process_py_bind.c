/** @file
  pymergetic.metal.process — Python view of the live-process table.

  Lets a µPy command started via pmcmd.<name>(...) (FACADE-shaped: fires
  a task and returns immediately, see docs/MICROPYTHON.md) observe its own
  completion with a plain polling loop, instead of needing new host logic.
**/
#include <pymergetic/metal/guest/process/process.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>

/* Only MicroPython header this file needs — see async_py_bind.c's comment. */
#include "py/obj.h"

static mp_obj_t py_process_poll(void)
{
  int32_t           status = 0;
  int               rc     = pm_metal_process_poll(&status);
  pm_metal_py_obj_t items[2];

  items[0] = pm_metal_py_int_new(rc);
  items[1] = pm_metal_py_int_new(status);
  return pm_metal_py_tuple_new(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_process_poll_obj, py_process_poll);
PM_METAL_PY_BIND(g_py_bind_process_poll,
                 "pymergetic.metal.process",
                 "poll",
                 py_process_poll_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t py_process_active(void)
{
  return pm_metal_py_int_new(pm_metal_process_active());
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_process_active_obj, py_process_active);
PM_METAL_PY_BIND(g_py_bind_process_active,
                 "pymergetic.metal.process",
                 "active",
                 py_process_active_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t py_process_current(void)
{
  return pm_metal_py_int_new((int64_t)pm_metal_process_current());
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_process_current_obj, py_process_current);
PM_METAL_PY_BIND(g_py_bind_process_current,
                 "pymergetic.metal.process",
                 "current",
                 py_process_current_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t py_process_info(mp_obj_t id_obj)
{
  pm_metal_process_info_t info;
  pm_metal_process_id_t   id = (pm_metal_process_id_t)pm_metal_py_int_get(id_obj);
  pm_metal_py_obj_t       d;

  if (pm_metal_process_info(id, &info) != 0) {
    return pm_metal_py_obj_none();
  }

  d = pm_metal_py_dict_new(6);
  pm_metal_py_dict_set_str(d, "id", pm_metal_py_int_new((int64_t)info.id));
  pm_metal_py_dict_set_str(d, "name", pm_metal_py_str_new(info.name));
  pm_metal_py_dict_set_str(d, "state", pm_metal_py_int_new((int64_t)info.state));
  pm_metal_py_dict_set_str(d, "ui_kind", pm_metal_py_int_new((int64_t)info.ui_kind));
  pm_metal_py_dict_set_str(d, "tab", pm_metal_py_int_new((int64_t)info.tab));
  pm_metal_py_dict_set_str(d, "surface", pm_metal_py_int_new((int64_t)info.surface));
  return d;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_process_info_obj, py_process_info);
PM_METAL_PY_BIND(g_py_bind_process_info,
                 "pymergetic.metal.process",
                 "info",
                 py_process_info_obj,
                 PM_METAL_PY_SYNC);
