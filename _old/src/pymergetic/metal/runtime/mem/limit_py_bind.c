/** @file
  pymergetic.metal.mem.limit.list() / .get(id) / .module(name)
**/
#include <pymergetic/metal/runtime/mem/limit.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>

#include "py/obj.h"

#include <string.h>

static pm_metal_py_obj_t LimToDict(const pm_metal_mem_limit_t *e)
{
  pm_metal_py_obj_t d;
  const char       *unit;
  const char       *note;

  unit = (e->unit != NULL) ? e->unit : "";
  note = (e->note != NULL) ? e->note : "";

  d = pm_metal_py_dict_new(6);
  pm_metal_py_dict_set_str(d, "id", pm_metal_py_str_new(e->id != NULL ? e->id : ""));
  pm_metal_py_dict_set_str(d, "module", pm_metal_py_str_new(e->module != NULL ? e->module : ""));
  pm_metal_py_dict_set_str(d, "name", pm_metal_py_str_new(e->name != NULL ? e->name : ""));
  pm_metal_py_dict_set_str(d, "value", pm_metal_py_int_new((int64_t)e->value));
  pm_metal_py_dict_set_str(d, "unit", pm_metal_py_str_new(unit));
  pm_metal_py_dict_set_str(d, "note", pm_metal_py_str_new(note));
  return d;
}

static mp_obj_t py_mem_limit_list(void)
{
  pm_metal_py_obj_t    list;
  pm_metal_mem_limit_t e;
  uint32_t             i;
  uint32_t             n;

  list = pm_metal_py_list_new();
  n    = pm_metal_mem_limit_count();
  for (i = 0u; i < n; i++) {
    if (pm_metal_mem_limit_get(i, &e) == 0) {
      pm_metal_py_list_append(list, LimToDict(&e));
    }
  }
  return (mp_obj_t)list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_mem_limit_list_obj, py_mem_limit_list);
PM_METAL_PY_BIND_DOC(g_py_bind_mem_limit_list,
                     "pymergetic.metal.mem.limit",
                     "list",
                     py_mem_limit_list_obj,
                     PM_METAL_PY_SYNC,
                     "List compile-time memory/buffer budget rows.",
                     "list() -> list[dict]",
                     "Each dict is {id, module, name, value, unit, note}. id is "
                     "'module.name' (e.g. net.asgi.ASGI_IO_MAX).");

static mp_obj_t py_mem_limit_get(mp_obj_t id_obj)
{
  const char          *id;
  pm_metal_mem_limit_t e;

  id = mp_obj_str_get_str(id_obj);
  if (pm_metal_mem_limit_find(id, &e) != 0) {
    return (mp_obj_t)pm_metal_py_obj_none();
  }
  return (mp_obj_t)LimToDict(&e);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_mem_limit_get_obj, py_mem_limit_get);
PM_METAL_PY_BIND_DOC(g_py_bind_mem_limit_get,
                     "pymergetic.metal.mem.limit",
                     "get",
                     py_mem_limit_get_obj,
                     PM_METAL_PY_SYNC,
                     "Lookup one budget row by id (module.name).",
                     "get(id: str) -> dict | None",
                     "Returns the same dict shape as list() entries, or None.");

static int32_t LimModuleMatch(const pm_metal_mem_limit_t *e, const char *key)
{
  size_t mlen;

  if (e == NULL || key == NULL || e->module == NULL) {
    return 0;
  }
  if (strcmp(e->module, key) == 0) {
    return 1;
  }
  mlen = strlen(key);
  if (mlen > 0u && strncmp(e->module, key, mlen) == 0 &&
      (e->module[mlen] == '\0' || e->module[mlen] == '.')) {
    return 1;
  }
  return 0;
}

static mp_obj_t py_mem_limit_module(mp_obj_t name_obj)
{
  pm_metal_py_obj_t    list;
  pm_metal_mem_limit_t e;
  const char          *name;
  uint32_t             i;
  uint32_t             n;

  name = mp_obj_str_get_str(name_obj);
  list = pm_metal_py_list_new();
  n    = pm_metal_mem_limit_count();
  for (i = 0u; i < n; i++) {
    if (pm_metal_mem_limit_get(i, &e) != 0) {
      continue;
    }
    if (LimModuleMatch(&e, name)) {
      pm_metal_py_list_append(list, LimToDict(&e));
    }
  }
  return (mp_obj_t)list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_mem_limit_module_obj, py_mem_limit_module);
PM_METAL_PY_BIND_DOC(g_py_bind_mem_limit_module,
                     "pymergetic.metal.mem.limit",
                     "module",
                     py_mem_limit_module_obj,
                     PM_METAL_PY_SYNC,
                     "List budget rows for a module name or prefix.",
                     "module(name: str) -> list[dict]",
                     "'net' matches net and net.asgi / net.ip / net.tls; exact "
                     "module names match only that module.");
