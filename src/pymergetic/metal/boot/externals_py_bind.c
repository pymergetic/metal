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

/* register(id, version, url="", note="") — host copies strings (GC-safe). */
static mp_obj_t py_externals_register(size_t n_args, const mp_obj_t *args)
{
  const char *id;
  const char *version;
  const char *url;
  const char *note;

  id      = mp_obj_str_get_str(args[0]);
  version = mp_obj_str_get_str(args[1]);
  url     = "";
  note    = "";
  if (n_args >= 3 && args[2] != mp_const_none) {
    url = mp_obj_str_get_str(args[2]);
  }
  if (n_args >= 4 && args[3] != mp_const_none) {
    note = mp_obj_str_get_str(args[3]);
  }
  if (pm_metal_external_register(id, version, url, note) != 0) {
    return mp_const_false;
  }
  return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_externals_register_obj, 2, 4, py_externals_register);
PM_METAL_PY_BIND_DOC(g_py_bind_externals_register,
                     "pymergetic.metal.externals",
                     "register",
                     py_externals_register_obj,
                     PM_METAL_PY_SYNC,
                     "Register or update a third-party stack id at runtime.",
                     "register(id: str, version: str, url: str = '', note: str = '') -> bool",
                     "Used by mods/*/autoload.py (and any guest code). Strings are copied "
                     "into the host dyn table; same id updates in place. False if the table "
                     "is full or id is empty.");
