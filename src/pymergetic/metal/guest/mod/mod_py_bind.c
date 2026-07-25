/** @file
  pymergetic.metal.mod.<name>.<func>(...) — real lazy attribute access over
  the mod registry (docs/MODS.md), not a string-keyed dispatch table.

  Two custom MicroPython object types, chained:
   - mod_ns: the "pymergetic.metal.mod" singleton itself. Any attribute
     load (mod.<name>) always succeeds and hands back a mod_name proxy —
     mod names load on demand (same "load if needed" contract as
     pm_metal_mod_func_resolve()), so there's nothing to validate yet.
   - mod_name proxy: attribute load (<name>.<func>) is where AttributeError
     actually happens — it ensures the mod is loaded then resolves
     mod_name.func_name via pm_metal_mod_func_resolve(); a bad name fails
     that resolve and the attr hook leaves dest untouched, so MicroPython's
     normal "attribute not found" path raises AttributeError, exactly like
     any real Python attribute (never at call time for a typo).

  Calling <name>.<func>() re-resolves fresh (cheap FuncFind, no live wasm
  call yet — mirrors boot_test.c's own "resolve once at callsite" comment,
  just re-done per call instead of cached, since a Python-held proxy can
  outlive a mod reset/unload), spawns a guest coro via pm_metal_mod_fn_coro,
  and returns a Metal awaitable (py_obj.h's pm_metal_py_new_awaitable_u32,
  shared with pymergetic.metal.aio) that parks the Python task's own step
  on that coro and carries the guest's pm_metal_async_set_result_u32()
  payload back as the `await` expression's value.

  Args: none yet — registered mod functions are async (i)i status(self_h)
  (docs/MODS.md), no user-passed arguments in this phase. Calling with
  arguments raises TypeError.
**/
#include <pymergetic/metal/guest/mod/mod.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>
#include <pymergetic/metal/runtime/async/async.h>

#include "py/mpstate.h"
#include "py/obj.h"
#include "py/runtime.h"

typedef struct {
  mp_obj_base_t base;
  qstr          mod_name;
  qstr          func_name;
} mod_func_obj_t;

static mp_obj_t mod_func_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args)
{
  mod_func_obj_t         *self = MP_OBJ_TO_PTR(self_in);
  pm_metal_mod_fn_t       fn;
  pm_metal_async_handle_t coro;

  (void)args;
  if (n_args != 0u || n_kw != 0u) {
    mp_raise_TypeError(MP_ERROR_TEXT("mod func takes no arguments (yet)"));
  }

  if (pm_metal_mod_func_resolve(qstr_str(self->mod_name), qstr_str(self->func_name), &fn) != 0) {
    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("mod func resolve failed"));
  }

  coro = pm_metal_mod_fn_coro(&fn);
  if (coro == PM_METAL_ASYNC_HANDLE_INVALID) {
    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("mod func coro create failed"));
  }

  return pm_metal_py_new_awaitable_u32(coro);
}

static MP_DEFINE_CONST_OBJ_TYPE(
  mod_func_type, MP_QSTR_function, MP_TYPE_FLAG_NONE, call, mod_func_call);

static mp_obj_t mod_func_new(qstr mod_name, qstr func_name)
{
  mod_func_obj_t *o = m_new_obj(mod_func_obj_t);
  o->base.type      = &mod_func_type;
  o->mod_name       = mod_name;
  o->func_name      = func_name;
  return MP_OBJ_FROM_PTR(o);
}

typedef struct {
  mp_obj_base_t base;
  qstr          mod_name;
} mod_name_obj_t;

static void mod_name_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest)
{
  mod_name_obj_t   *self = MP_OBJ_TO_PTR(self_in);
  pm_metal_mod_fn_t fn;

  if (dest[0] != MP_OBJ_NULL) {
    return; /* store/delete unsupported — leave dest, caller raises */
  }

  if (pm_metal_mod_func_resolve(qstr_str(self->mod_name), qstr_str(attr), &fn) != 0) {
    return; /* AttributeError via normal "not found" path */
  }

  dest[0] = mod_func_new(self->mod_name, attr);
}

static MP_DEFINE_CONST_OBJ_TYPE(
  mod_name_type, MP_QSTR_object, MP_TYPE_FLAG_NONE, attr, mod_name_attr);

static mp_obj_t mod_name_new(qstr mod_name)
{
  mod_name_obj_t *o = m_new_obj(mod_name_obj_t);
  o->base.type       = &mod_name_type;
  o->mod_name        = mod_name;
  return MP_OBJ_FROM_PTR(o);
}

static void mod_ns_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest)
{
  (void)self_in;
  if (dest[0] != MP_OBJ_NULL) {
    return; /* store/delete unsupported */
  }
  dest[0] = mod_name_new(attr);
}

static MP_DEFINE_CONST_OBJ_TYPE(mod_ns_type, MP_QSTR_object, MP_TYPE_FLAG_NONE, attr, mod_ns_attr);

static const mp_obj_base_t g_mod_ns_obj = { &mod_ns_type };

void pm_metal_py_mod_install(void)
{
  nlr_buf_t nlr;

  if (nlr_push(&nlr) == 0) {
    pm_metal_py_obj_t parent = pm_metal_py_bind_resolve_module("pymergetic.metal");
    if (parent != NULL) {
      mp_store_attr((mp_obj_t)parent, qstr_from_str("mod"), MP_OBJ_FROM_PTR(&g_mod_ns_obj));

      /*
       * mp_obj_new_module()'s own trick, replicated for a non-mp_type_module
       * singleton: `import a.b.mod as x` short-circuits through
       * mp_loaded_modules_dict (builtinimport.c's process_import_at_level)
       * before ever trying the filesystem, same as every real submodule —
       * the parent-attribute store above only covers the bare
       * `import a.b; a.b.mod.foo` attribute-chain form.
       */
      {
        mp_map_t      *modules_map = &MP_STATE_VM(mp_loaded_modules_dict).map;
        mp_map_elem_t *el          = mp_map_lookup(modules_map,
                                                   MP_OBJ_NEW_QSTR(qstr_from_str("pymergetic.metal.mod")),
                                                   MP_MAP_LOOKUP_ADD_IF_NOT_FOUND);
        el->value = MP_OBJ_FROM_PTR(&g_mod_ns_obj);
      }
    }
    nlr_pop();
  }
}
