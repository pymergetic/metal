/** @file
  pymergetic.metal.authors.about() — Metal's own about record, in Python.

  A plain PM_METAL_PY_BIND callable, not an attribute on the mod registry's
  namespace (pymergetic.metal.mod.<name> resolves *every* attribute as a
  mod name to load — see mod_py_bind.c's mod_ns_attr — so "mod.about"
  would try to load a mod literally named "about" instead of reaching
  this). A dedicated pymergetic.metal.authors submodule mirrors the WASI
  side's own split (pymergetic.metal.mod vs pymergetic.metal.authors).

  Built purely through py_obj.h's host-only facade (pm_metal_py_dict_new
  et al.) — unlike mod_py_bind.c this needs no custom MicroPython object
  types of its own, so it never has to include py/runtime.h.
**/
#include <pymergetic/metal/boot/authors.h>
#include <pymergetic/metal/guest/mod/mod_lifecycle.h> /* pm_metal_mod_author_role_name */
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>

#include "py/obj.h"

/**
 * {"version": str, "desc": str, "url": str, "authors": [{"name", "email", "role"}, ...]}
 * — same shape as mod_py_bind.c's mod_about_dict(), field-for-field, kept
 * as a separate small copy rather than a shared export: the two callers
 * sit in different layers (guest/mod's lazy per-mod attribute proxy vs.
 * this module's plain sync callable) and each is only a dozen lines.
 */
static mp_obj_t py_authors_about(void)
{
  const pm_metal_mod_about_t *about = pm_metal_kernel_about();
  pm_metal_py_obj_t           authors;
  pm_metal_py_obj_t           d;
  uint32_t                    i;

  authors = pm_metal_py_list_new();
  for (i = 0; i < about->author_count; i++) {
    const pm_metal_mod_author_t *a         = &about->authors[i];
    const char                  *role_name = pm_metal_mod_author_role_name(a->role);
    pm_metal_py_obj_t            rec       = pm_metal_py_dict_new(3);

    pm_metal_py_dict_set_str(rec, "name", pm_metal_py_str_new(a->name));
    pm_metal_py_dict_set_str(rec, "email", pm_metal_py_str_new(a->email));
    pm_metal_py_dict_set_str(rec, "role", pm_metal_py_str_new(role_name));
    pm_metal_py_list_append(authors, rec);
  }

  d = pm_metal_py_dict_new(4);
  pm_metal_py_dict_set_str(d, "version", pm_metal_py_str_new(about->version));
  pm_metal_py_dict_set_str(d, "desc", pm_metal_py_str_new(about->desc));
  pm_metal_py_dict_set_str(d, "url", pm_metal_py_str_new(about->url));
  pm_metal_py_dict_set_str(d, "authors", authors);
  return (mp_obj_t)d;
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_authors_about_obj, py_authors_about);
PM_METAL_PY_BIND(g_py_bind_authors_about,
                 "pymergetic.metal.authors",
                 "about",
                 py_authors_about_obj,
                 PM_METAL_PY_SYNC);
