/** @file
  pymergetic.metal.externals.list() / .get(id) — third-party stack identity.
**/
#include <pymergetic/metal/boot/externals.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>

#include "py/obj.h"

static pm_metal_py_obj_t ExtToDict(const pm_metal_external_t *e)
{
  pm_metal_py_obj_t d;
  const char       *url;
  const char       *note;

  url  = (e->url != NULL) ? e->url : "";
  note = (e->note != NULL) ? e->note : "";

  d = pm_metal_py_dict_new(4);
  pm_metal_py_dict_set_str(d, "id", pm_metal_py_str_new(e->id != NULL ? e->id : ""));
  pm_metal_py_dict_set_str(d, "version",
                           pm_metal_py_str_new(e->version != NULL ? e->version : ""));
  pm_metal_py_dict_set_str(d, "url", pm_metal_py_str_new(url));
  pm_metal_py_dict_set_str(d, "note", pm_metal_py_str_new(note));
  return d;
}

static mp_obj_t py_externals_list(void)
{
  pm_metal_py_obj_t   list;
  pm_metal_external_t e;
  uint32_t            i;
  uint32_t            n;

  list = pm_metal_py_list_new();
  n    = pm_metal_external_count();
  for (i = 0u; i < n; i++) {
    if (pm_metal_external_get(i, &e) == 0) {
      pm_metal_py_list_append(list, ExtToDict(&e));
    }
  }
  return (mp_obj_t)list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_externals_list_obj, py_externals_list);
PM_METAL_PY_BIND(g_py_bind_externals_list,
                 "pymergetic.metal.externals",
                 "list",
                 py_externals_list_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t py_externals_get(mp_obj_t id_obj)
{
  const char         *id;
  pm_metal_external_t e;

  id = mp_obj_str_get_str(id_obj);
  if (pm_metal_external_find(id, &e) != 0) {
    return (mp_obj_t)pm_metal_py_obj_none();
  }
  return (mp_obj_t)ExtToDict(&e);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_externals_get_obj, py_externals_get);
PM_METAL_PY_BIND(g_py_bind_externals_get,
                 "pymergetic.metal.externals",
                 "get",
                 py_externals_get_obj,
                 PM_METAL_PY_SYNC);
