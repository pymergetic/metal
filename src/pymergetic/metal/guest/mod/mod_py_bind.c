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

  Phase 2d — isolated/FRESH instances (<name>.fresh()): mod_name.fresh is
  a callable (mod_fresh_factory) that opens a private instance via
  pm_metal_mod_fresh_open and returns a mod_fresh_scope object. That
  object's own attr hook resolves __aenter__/__aexit__ (so `async with`
  works via MicroPython's generic attribute dispatch — no separate
  context-manager protocol needed) as well as per-function bound calls
  resolved against the fresh instance (pm_metal_mod_fresh_resolve),
  cached once per attribute access since the instance's fn pointers stay
  valid for the scope's whole lifetime:

    async with pymergetic.metal.mod.acme.fresh() as inst:
        await inst.frobnicate()

  __aenter__ yields the scope object itself as VAR (no real Metal op to
  await — opening already happened synchronously in fresh()); __aexit__
  closes the instance and returns false (never suppresses an exception).
  Both use mod_fresh_instant, a trivial "already resolved" awaitable —
  distinct from py_await.c's metal_aw_type since there's no real async
  handle to park on here.
**/
#include <pymergetic/metal/guest/mod/mod.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/runtime/mem/mem.h>

#include <stdio.h>
#include <string.h>

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

/**
 * Load-only __doc__ (docs/DOC_IFACE_PLAN.md Part I "mod: metal.mod.*.__doc__"
 * reader) — joined summary/sig/body from pm_metal_mod_register_func_doc,
 * "" (never set) if the mod only called plain register_func. Any other
 * attribute (store, or a name that isn't __doc__) falls through to the
 * normal "not found" path.
 */
static void mod_func_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest)
{
  mod_func_obj_t *self = MP_OBJ_TO_PTR(self_in);
  const char     *summary;
  const char     *sig;
  const char     *body;
  char            buf[300];
  size_t          off;

  if (dest[0] != MP_OBJ_NULL || attr != qstr_from_str("__doc__")) {
    return;
  }

  if (pm_metal_mod_func_doc_get(qstr_str(self->mod_name), qstr_str(self->func_name), &summary,
                                &sig, &body) != 0) {
    return;
  }

  off = 0;
  if (summary[0] != '\0') {
    off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s", summary);
  }
  if (sig[0] != '\0') {
    off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s%s", off ? "\n\n" : "", sig);
  }
  if (body[0] != '\0') {
    off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s%s", off ? "\n\n" : "", body);
  }

  dest[0] = mp_obj_new_str(buf, off);
}

static MP_DEFINE_CONST_OBJ_TYPE(
  mod_func_type, MP_QSTR_function, MP_TYPE_FLAG_NONE, call, mod_func_call, attr, mod_func_attr);

static mp_obj_t mod_func_new(qstr mod_name, qstr func_name)
{
  mod_func_obj_t *o = m_new_obj(mod_func_obj_t);
  o->base.type      = &mod_func_type;
  o->mod_name       = mod_name;
  o->func_name      = func_name;
  return MP_OBJ_FROM_PTR(o);
}

/* --- Phase 2d: isolated/FRESH instances --- */

/* Trivial "already resolved" awaitable — StopIteration(value) on the
 * first (and only) iternext, no park. Distinct from py_await.c's
 * metal_aw_type: __aenter__/__aexit__ have no real Metal handle to
 * await, just a value to hand back through `await`. */
typedef struct {
  mp_obj_base_t base;
  mp_obj_t      value;
} mod_fresh_instant_obj_t;

static mp_obj_t mod_fresh_instant_iternext(mp_obj_t self_in)
{
  mod_fresh_instant_obj_t *self = MP_OBJ_TO_PTR(self_in);
  return mp_make_stop_iteration(self->value);
}

static MP_DEFINE_CONST_OBJ_TYPE(mod_fresh_instant_type,
                                MP_QSTR_object,
                                MP_TYPE_FLAG_ITER_IS_ITERNEXT,
                                iter,
                                mod_fresh_instant_iternext);

static mp_obj_t mod_fresh_instant_new(mp_obj_t value)
{
  mod_fresh_instant_obj_t *o = m_new_obj(mod_fresh_instant_obj_t);
  o->base.type               = &mod_fresh_instant_type;
  o->value                   = value;
  return MP_OBJ_FROM_PTR(o);
}

/* A function bound against a specific fresh instance — cached fn (valid
 * for the scope's lifetime), no per-call re-resolve unlike mod_func_t. */
typedef struct {
  mp_obj_base_t     base;
  pm_metal_mod_fn_t fn;
} mod_fresh_func_obj_t;

static mp_obj_t mod_fresh_func_call(mp_obj_t        self_in,
                                    size_t          n_args,
                                    size_t          n_kw,
                                    const mp_obj_t *args)
{
  mod_fresh_func_obj_t   *self = MP_OBJ_TO_PTR(self_in);
  pm_metal_async_handle_t coro;

  (void)args;
  if (n_args != 0u || n_kw != 0u) {
    mp_raise_TypeError(MP_ERROR_TEXT("mod func takes no arguments (yet)"));
  }

  coro = pm_metal_mod_fn_coro(&self->fn);
  if (coro == PM_METAL_ASYNC_HANDLE_INVALID) {
    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("mod func coro create failed"));
  }

  return pm_metal_py_new_awaitable_u32(coro);
}

static MP_DEFINE_CONST_OBJ_TYPE(
  mod_fresh_func_type, MP_QSTR_function, MP_TYPE_FLAG_NONE, call, mod_fresh_func_call);

static mp_obj_t mod_fresh_func_new(const pm_metal_mod_fn_t *fn)
{
  mod_fresh_func_obj_t *o = m_new_obj(mod_fresh_func_obj_t);
  o->base.type            = &mod_fresh_func_type;
  o->fn                   = *fn;
  return MP_OBJ_FROM_PTR(o);
}

/* __aenter__ / __aexit__ bound method — "which" picks the behavior,
 * "scope" closes over the fresh_scope object (already resolved at
 * attribute-access time, so the call itself takes no self arg). */
typedef struct {
  mp_obj_base_t base;
  mp_obj_t      scope;
  int           which; /* 0 = __aenter__, 1 = __aexit__ */
} mod_fresh_bound_obj_t;

static mp_obj_t mod_fresh_bound_call(mp_obj_t        self_in,
                                     size_t          n_args,
                                     size_t          n_kw,
                                     const mp_obj_t *args);

static MP_DEFINE_CONST_OBJ_TYPE(
  mod_fresh_bound_type, MP_QSTR_function, MP_TYPE_FLAG_NONE, call, mod_fresh_bound_call);

static mp_obj_t mod_fresh_bound_new(mp_obj_t scope, int which)
{
  mod_fresh_bound_obj_t *o = m_new_obj(mod_fresh_bound_obj_t);
  o->base.type             = &mod_fresh_bound_type;
  o->scope                 = scope;
  o->which                 = which;
  return MP_OBJ_FROM_PTR(o);
}

typedef struct {
  mp_obj_base_t          base;
  pm_metal_mod_fresh_h_t h;
  qstr                   mod_name;
} mod_fresh_scope_obj_t;

static mp_obj_t mod_fresh_bound_call(mp_obj_t        self_in,
                                     size_t          n_args,
                                     size_t          n_kw,
                                     const mp_obj_t *args)
{
  mod_fresh_bound_obj_t *self  = MP_OBJ_TO_PTR(self_in);
  mod_fresh_scope_obj_t *scope = MP_OBJ_TO_PTR(self->scope);

  (void)n_args;
  (void)n_kw;
  (void)args;
  if (self->which == 0) {
    /* __aenter__() -> VAR: hand back the scope object itself. */
    return mod_fresh_instant_new(self->scope);
  }

  /* __aexit__(exc_type, exc, tb) -> never suppress. */
  pm_metal_mod_fresh_close(scope->h);
  scope->h = PM_METAL_MOD_FRESH_H_INVALID;
  return mod_fresh_instant_new(mp_const_false);
}

static void mod_fresh_scope_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest)
{
  mod_fresh_scope_obj_t *self = MP_OBJ_TO_PTR(self_in);
  pm_metal_mod_fn_t      fn;

  if (dest[0] != MP_OBJ_NULL) {
    return; /* store/delete unsupported */
  }

  if (attr == qstr_from_str("__aenter__")) {
    dest[0] = mod_fresh_bound_new(self_in, 0);
    return;
  }
  if (attr == qstr_from_str("__aexit__")) {
    dest[0] = mod_fresh_bound_new(self_in, 1);
    return;
  }

  if (self->h == PM_METAL_MOD_FRESH_H_INVALID) {
    return; /* closed — AttributeError via normal "not found" path */
  }
  if (pm_metal_mod_fresh_resolve(self->h, qstr_str(attr), &fn) != 0) {
    return;
  }

  dest[0] = mod_fresh_func_new(&fn);
}

static MP_DEFINE_CONST_OBJ_TYPE(
  mod_fresh_scope_type, MP_QSTR_object, MP_TYPE_FLAG_NONE, attr, mod_fresh_scope_attr);

static mp_obj_t mod_fresh_scope_new(pm_metal_mod_fresh_h_t h, qstr mod_name)
{
  mod_fresh_scope_obj_t *o = m_new_obj(mod_fresh_scope_obj_t);
  o->base.type             = &mod_fresh_scope_type;
  o->h                     = h;
  o->mod_name              = mod_name;
  return MP_OBJ_FROM_PTR(o);
}

typedef struct {
  mp_obj_base_t base;
  qstr          mod_name;
} mod_fresh_factory_obj_t;

static mp_obj_t mod_fresh_factory_call(mp_obj_t        self_in,
                                       size_t          n_args,
                                       size_t          n_kw,
                                       const mp_obj_t *args)
{
  mod_fresh_factory_obj_t *self = MP_OBJ_TO_PTR(self_in);
  pm_metal_mod_fresh_h_t   h;

  (void)args;
  if (n_args != 0u || n_kw != 0u) {
    mp_raise_TypeError(MP_ERROR_TEXT("mod.fresh() takes no arguments"));
  }

  h = pm_metal_mod_fresh_open(qstr_str(self->mod_name));
  if (h == PM_METAL_MOD_FRESH_H_INVALID) {
    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("mod fresh_open failed"));
  }

  return mod_fresh_scope_new(h, self->mod_name);
}

static MP_DEFINE_CONST_OBJ_TYPE(
  mod_fresh_factory_type, MP_QSTR_function, MP_TYPE_FLAG_NONE, call, mod_fresh_factory_call);

static mp_obj_t mod_fresh_factory_new(qstr mod_name)
{
  mod_fresh_factory_obj_t *o = m_new_obj(mod_fresh_factory_obj_t);
  o->base.type               = &mod_fresh_factory_type;
  o->mod_name                = mod_name;
  return MP_OBJ_FROM_PTR(o);
}

typedef struct {
  mp_obj_base_t base;
  qstr          mod_name;
} mod_name_obj_t;

/**
 * {"version": str, "desc": str, "url": str, "authors": [{"name", "email", "role"}, ...]}
 * — one shot, mirrors pm_metal_mod_about_t field-for-field. Native host
 * binding, not a wasm import, so unlike the guest ABI's role uint32_t
 * this can just hand Python a readable role string directly.
 *
 * Dict keys are runtime-interned via qstr_from_str() + MP_OBJ_NEW_QSTR(),
 * not compile-time MP_QSTR_xxx constants — this embed port's qstr table
 * is pre-generated from a fixed file list (py/embed/micropython_embed.mk's
 * SRC_QSTR) that does not scan guest/mod/mod_py_bind.c, same reason
 * mod_ns_attr/mod_name_attr below resolve attribute names ("fresh",
 * mod names, ...) the same way instead of MP_QSTR_*.
 */
static mp_obj_t mod_about_dict(const pm_metal_mod_about_t *about)
{
  mp_obj_t d;
  mp_obj_t authors;
  uint32_t i;

  authors = mp_obj_new_list(0, NULL);
  for (i = 0; i < about->author_count; i++) {
    const pm_metal_mod_author_t *a         = &about->authors[i];
    const char                  *role_name = pm_metal_mod_author_role_name(a->role);
    mp_obj_t                     rec       = mp_obj_new_dict(3);

    mp_obj_dict_store(
      rec, MP_OBJ_NEW_QSTR(qstr_from_str("name")), mp_obj_new_str(a->name, strlen(a->name)));
    mp_obj_dict_store(
      rec, MP_OBJ_NEW_QSTR(qstr_from_str("email")), mp_obj_new_str(a->email, strlen(a->email)));
    mp_obj_dict_store(
      rec, MP_OBJ_NEW_QSTR(qstr_from_str("role")), mp_obj_new_str(role_name, strlen(role_name)));
    mp_obj_list_append(authors, rec);
  }

  d = mp_obj_new_dict(4);
  mp_obj_dict_store(d,
                    MP_OBJ_NEW_QSTR(qstr_from_str("version")),
                    mp_obj_new_str(about->version, strlen(about->version)));
  mp_obj_dict_store(
    d, MP_OBJ_NEW_QSTR(qstr_from_str("desc")), mp_obj_new_str(about->desc, strlen(about->desc)));
  mp_obj_dict_store(
    d, MP_OBJ_NEW_QSTR(qstr_from_str("url")), mp_obj_new_str(about->url, strlen(about->url)));
  mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(qstr_from_str("authors")), authors);
  return d;
}

static void mod_name_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest)
{
  mod_name_obj_t   *self = MP_OBJ_TO_PTR(self_in);
  pm_metal_mod_fn_t fn;

  if (dest[0] != MP_OBJ_NULL) {
    return; /* store/delete unsupported — leave dest, caller raises */
  }

  if (attr == qstr_from_str("fresh")) {
    dest[0] = mod_fresh_factory_new(self->mod_name);
    return;
  }

  if (attr == qstr_from_str("about")) {
    /* Heap temp, not a stack local — pm_metal_mod_about_t is ~2.7 KB
     * (mostly desc), see mod_types.h. */
    pm_metal_mod_about_t *about = (pm_metal_mod_about_t *)pm_metal_mem_alloc(
      sizeof(*about), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);

    if (about == NULL) {
      return; /* OOM — AttributeError via normal "not found" path */
    }

    if (pm_metal_mod_about_get(qstr_str(self->mod_name), about) != 0) {
      pm_metal_mem_free(about);
      return; /* not a known mod — AttributeError via normal "not found" path */
    }

    dest[0] = mod_about_dict(about);
    pm_metal_mem_free(about);
    return;
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
  o->base.type      = &mod_name_type;
  o->mod_name       = mod_name;
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
        el->value                  = MP_OBJ_FROM_PTR(&g_mod_ns_obj);
      }
    }
    nlr_pop();
  }
}
