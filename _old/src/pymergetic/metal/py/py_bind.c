/** @file
  C → Python bind table — linker-section auto-register (mirrors
  PM_METAL_SHELL_CMD/shell_cmd.c) + generic installer.

  Each PM_METAL_PY_BIND row names a dotted module path ("pymergetic.metal.aio")
  and an attribute name ("sleep_us") to attach an already-built MicroPython
  callable under. Call sites get real attribute access — mod.func(...), never
  a string-literal dispatch table — because the callable is wired directly
  into the target module's globals dict as a genuine attribute.
**/
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>

#include "py/obj.h"
#include "py/runtime.h"

extern const pm_metal_py_bind_t __pm_metal_py_binds_start[];
extern const pm_metal_py_bind_t __pm_metal_py_binds_end[];

/*
 * Resolve (creating as needed) the module object for a dotted path,
 * wiring mp_loaded_modules_dict + parent->child attributes at every level
 * so both `import a.b.c as x` (dict short-circuit in
 * process_import_at_level) and bare `import a.b.c` + `a.b.c.foo()`
 * (attribute chain) work, and so pm_metal_py_lookup's own dict-then-attr
 * walk keeps working against C-registered modules exactly like it does
 * against real file-backed packages.
 *
 * Exposed via py_obj.h (not static) so other C-side install helpers outside
 * py/ (mod_py_bind.c's pymergetic.metal.mod, ...) can wire a non-callable
 * singleton object under a dotted path the same way PM_METAL_PY_BIND wires
 * a callable — see pm_metal_py_mod_install. Returns pm_metal_py_obj_t (not
 * mp_obj_t) since this is the one function of py_bind.c's that a
 * consumer outside py/ calls directly.
 */
pm_metal_py_obj_t pm_metal_py_bind_resolve_module(const char *dotted)
{
  size_t   len;
  size_t   pos;
  mp_obj_t parent;
  mp_obj_t cur;

  if (dotted == NULL) {
    return MP_OBJ_NULL;
  }
  len = strlen(dotted);
  if (len == 0) {
    return MP_OBJ_NULL;
  }

  parent = MP_OBJ_NULL;
  cur    = MP_OBJ_NULL;
  pos    = 0;
  for (;;) {
    const char *dot     = strchr(dotted + pos, '.');
    size_t      seg_end = (dot != NULL) ? (size_t)(dot - dotted) : len;

    if (seg_end == pos) {
      return MP_OBJ_NULL; /* empty segment: leading/trailing/double dot */
    }

    cur = mp_obj_new_module(qstr_from_strn(dotted, seg_end));

    if (parent != MP_OBJ_NULL) {
      mp_store_attr(parent, qstr_from_strn(dotted + pos, seg_end - pos), cur);
    }
    if (dot == NULL) {
      return cur;
    }

    /* Non-leaf: mark as a package. MicroPython's import (builtinimport.c)
     * treats __path__ as a single STRING directory path and passes it to
     * mp_obj_str_get_str() — a CPython-style list here TypeErrors with
     * "can't convert 'list' object to str implicitly" on the next
     * submodule import (e.g. import pymergetic.metal.net.asgi). Use an
     * empty string: submodules are already registered in sys.modules by
     * this walk, so the FS search under "" is never needed. */
    {
      mp_obj_t dest[2];
      mp_load_method_maybe(cur, MP_QSTR___path__, dest);
      if (dest[0] == MP_OBJ_NULL) {
        mp_store_attr(cur, MP_QSTR___path__, MP_OBJ_NEW_QSTR(MP_QSTR_));
      }
    }

    parent = cur;
    pos    = seg_end + 1u;
  }
}

/**
 * Best-effort __doc__ install for one just-wired bind row
 * (docs/DOC_IFACE_PLAN.md Part I). Joined summary/sig/body, "\n\n"
 * between non-NULL parts. Native builtin function objects
 * (MP_DEFINE_CONST_FUN_OBJ_*) have no `attr` slot in this MicroPython
 * config, so mp_store_attr() on one always raises AttributeError — this
 * is genuinely a "try": failure here is expected and silent, own
 * nlr_push so it never aborts the row's real bind (already done by the
 * caller) or any later row. doc.lookup("py", ...) (util/doc.c) is the
 * reliable reader of these same three fields regardless of whether this
 * attempt stuck.
 */
static void PyBindTrySetDoc(const pm_metal_py_bind_t *row)
{
  nlr_buf_t nlr;
  char      buf[400];
  size_t    off;

  if (row->summary == NULL && row->sig == NULL && row->body == NULL) {
    return;
  }

  off = 0;
  if (row->summary != NULL) {
    off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s", row->summary);
  }
  if (row->sig != NULL) {
    off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s%s", off ? "\n\n" : "", row->sig);
  }
  if (row->body != NULL) {
    off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s%s", off ? "\n\n" : "", row->body);
  }

  if (nlr_push(&nlr) == 0) {
    mp_store_attr((mp_obj_t)row->fn, qstr_from_str("__doc__"), mp_obj_new_str(buf, off));
    nlr_pop();
  }
  /* else: no attr slot on this object's type — expected, ignore. */
}

int pm_metal_py_bind_table(const pm_metal_py_bind_t *rows, size_t n)
{
  nlr_buf_t nlr;
  size_t    i;
  int       rc;

  if (rows == NULL) {
    return (n == 0u) ? 0 : -1;
  }

  if (nlr_push(&nlr) == 0) {
    for (i = 0; i < n; i++) {
      mp_obj_t mod_obj;

      /*
       * rows[i].class_ is reflection metadata (used by the future .pyi
       * stub generator to know sync vs. async vs. facade signatures) —
       * the callable itself already behaves correctly on its own (a
       * MicroPython async-def already returns a coroutine), so the
       * installer just wires the attribute and doesn't branch on it.
       */
      if (rows[i].mod == NULL || rows[i].name == NULL || rows[i].fn == NULL) {
        nlr_pop();
        return -1;
      }

      mod_obj = pm_metal_py_bind_resolve_module(rows[i].mod);
      if (mod_obj == MP_OBJ_NULL) {
        nlr_pop();
        return -1;
      }

      mp_store_attr(mod_obj, qstr_from_str(rows[i].name), MP_OBJ_FROM_PTR(rows[i].fn));
      PyBindTrySetDoc(&rows[i]);
    }
    nlr_pop();
    rc = 0;
  } else {
    rc = -1;
  }
  return rc;
}

void pm_metal_py_binds_install(void)
{
  size_t n = (size_t)(__pm_metal_py_binds_end - __pm_metal_py_binds_start);
  (void)pm_metal_py_bind_table(__pm_metal_py_binds_start, n);
}
