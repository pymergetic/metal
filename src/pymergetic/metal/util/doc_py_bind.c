/** @file
  pymergetic.metal.doc — Python face over util/doc.h's catalog
  (docs/DOC_IFACE_PLAN.md Part I-E). Read-only: every function here is a
  thin translation of a pm_metal_doc_view_t into a Python dict, never a
  second copy of the summary/sig/body text (util/doc.c is the one
  reader of every underlying home).
**/
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>
#include <pymergetic/metal/util/doc.h>

#include "py/obj.h"

static const char *KindName(pm_metal_doc_kind_t kind)
{
  switch (kind) {
    case PM_METAL_DOC_SHELL:
      return "shell";
    case PM_METAL_DOC_PY:
      return "py";
    case PM_METAL_DOC_MOD:
      return "mod";
    default:
      return "";
  }
}

static int32_t KindFromStr(const char *name)
{
  if (name == NULL) {
    return -1;
  }
  if (strcmp(name, "shell") == 0) {
    return PM_METAL_DOC_SHELL;
  }
  if (strcmp(name, "py") == 0) {
    return PM_METAL_DOC_PY;
  }
  if (strcmp(name, "mod") == 0) {
    return PM_METAL_DOC_MOD;
  }
  return -1;
}

static mp_obj_t DocViewDict(const pm_metal_doc_view_t *view)
{
  mp_obj_t d = pm_metal_py_dict_new(5);

  pm_metal_py_dict_set_str(d, "kind", pm_metal_py_str_new(KindName(view->kind)));
  pm_metal_py_dict_set_str(d, "key", pm_metal_py_str_new(view->key));
  pm_metal_py_dict_set_str(d, "summary", pm_metal_py_str_new(view->summary));
  pm_metal_py_dict_set_str(d, "sig", pm_metal_py_str_new(view->sig));
  pm_metal_py_dict_set_str(d, "body", pm_metal_py_str_new(view->body));
  return d;
}

/**
 * Shell row surfaced under kind=py as pmcmd.<name> — copies the same
 * summary/sig/body pointers' text into Python strings (still one C home).
 */
static mp_obj_t DocPmcmdAliasDict(const pm_metal_doc_view_t *shell_view)
{
  char     key[96];
  mp_obj_t d = pm_metal_py_dict_new(5);

  snprintf(key, sizeof(key), "pmcmd.%s", shell_view->key != NULL ? shell_view->key : "");
  pm_metal_py_dict_set_str(d, "kind", pm_metal_py_str_new("py"));
  pm_metal_py_dict_set_str(d, "key", pm_metal_py_str_new(key));
  pm_metal_py_dict_set_str(d, "summary", pm_metal_py_str_new(shell_view->summary));
  pm_metal_py_dict_set_str(d, "sig", pm_metal_py_str_new(shell_view->sig));
  pm_metal_py_dict_set_str(d, "body", pm_metal_py_str_new(shell_view->body));
  return d;
}

static mp_obj_t py_doc_lookup(mp_obj_t kind_obj, mp_obj_t key_obj)
{
  const char          *kind_str = mp_obj_str_get_str(kind_obj);
  const char          *key      = mp_obj_str_get_str(key_obj);
  int32_t              kind     = KindFromStr(kind_str);
  pm_metal_doc_view_t  view;

  if (kind < 0) {
    pm_metal_py_raise_value_error("doc: kind must be 'shell'/'py'/'mod'");
  }

  if (pm_metal_doc_lookup((pm_metal_doc_kind_t)kind, key, &view) != 0) {
    return pm_metal_py_obj_none();
  }

  return DocViewDict(&view);
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_doc_lookup_obj, py_doc_lookup);
PM_METAL_PY_BIND_DOC(g_py_bind_doc_lookup,
                    "pymergetic.metal.doc",
                    "lookup",
                    py_doc_lookup_obj,
                    PM_METAL_PY_SYNC,
                    "Direct (kind, key) catalog lookup.",
                    "lookup(kind: str, key: str) -> dict | None",
                    "kind is 'shell'/'py'/'mod'; key is a shell command name, a dotted "
                    "py bind path (e.g. 'pymergetic.metal.fs.open'), 'pmcmd.<cmd>' when "
                    "kind='py', or 'modname.funcname'. Returns None if unknown, else "
                    "{kind, key, summary, sig, body} (sig/body are '' when unset).");

static mp_obj_t py_doc_lookup_key(mp_obj_t doc_key_obj)
{
  const char          *doc_key = mp_obj_str_get_str(doc_key_obj);
  pm_metal_doc_view_t  view;

  if (pm_metal_doc_lookup_key(doc_key, &view) != 0) {
    return pm_metal_py_obj_none();
  }

  return DocViewDict(&view);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_doc_lookup_key_obj, py_doc_lookup_key);
PM_METAL_PY_BIND_DOC(g_py_bind_doc_lookup_key,
                    "pymergetic.metal.doc",
                    "lookup_key",
                    py_doc_lookup_key_obj,
                    PM_METAL_PY_SYNC,
                    "Catalog lookup by one packed \"<kind>:<key>\" doc_key string.",
                    "lookup_key(doc_key: str) -> dict | None",
                    "Same result shape as lookup(); doc_key is what an iface sym table's "
                    "own doc_key column points at (see pymergetic.metal.iface.sym()).");

/*
 * Positional-only kind (not a `kind=` keyword) — this embed port's qstr
 * table is pre-generated from a fixed file list that doesn't scan this
 * file (see mod_py_bind.c's mod_ns_attr comment for the same
 * constraint), so there is no MP_QSTR_kind to accept a keyword arg by.
 */
static mp_obj_t py_doc_list(size_t n_args, const mp_obj_t *args)
{
  int32_t  kind  = -1;
  int32_t  total = pm_metal_doc_count();
  int32_t  i;
  mp_obj_t out   = pm_metal_py_list_new();

  if (n_args >= 1 && !pm_metal_py_obj_is_none((pm_metal_py_obj_t)args[0])) {
    kind = KindFromStr(mp_obj_str_get_str(args[0]));
    if (kind < 0) {
      pm_metal_py_raise_value_error("doc: kind must be 'shell'/'py'/'mod'");
    }
  }

  for (i = 0; i < total; i++) {
    pm_metal_doc_view_t view;

    if (pm_metal_doc_at((uint32_t)i, &view) != 0) {
      continue;
    }
    if (kind >= 0 && view.kind != (pm_metal_doc_kind_t)kind) {
      continue;
    }

    pm_metal_py_list_append(out, DocViewDict(&view));
  }

  /*
   * kind=py also lists shell cmds as pmcmd.<name> (Python face of the
   * same home). kind=all keeps a single shell row — no duplicate.
   */
  if (kind == PM_METAL_DOC_PY) {
    for (i = 0; i < total; i++) {
      pm_metal_doc_view_t view;

      if (pm_metal_doc_at((uint32_t)i, &view) != 0) {
        continue;
      }
      if (view.kind != PM_METAL_DOC_SHELL) {
        continue;
      }
      pm_metal_py_list_append(out, DocPmcmdAliasDict(&view));
    }
  }

  return out;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_doc_list_obj, 0, 1, py_doc_list);
PM_METAL_PY_BIND_DOC(g_py_bind_doc_list,
                    "pymergetic.metal.doc",
                    "list",
                    py_doc_list_obj,
                    PM_METAL_PY_SYNC,
                    "Enumerate the catalog; kind='py' also includes pmcmd.* aliases.",
                    "list(kind: str | None = None) -> list[dict]",
                    "kind is positional. kind='py' = pymergetic.metal.* binds plus "
                    "pmcmd.<cmd> (same text as kind='shell'). kind=None is each home "
                    "once (shell rows stay under shell, not duplicated as pmcmd).");

/**
 * Best-effort "help by bare name" (docs/DOC_IFACE_PLAN.md preview's
 * `doc.help("mem")`) — no kind given, so this tries the shell face
 * first (the overwhelmingly common case: a name someone just typed at
 * the prompt), then falls back to a linear scan of the whole catalog
 * for an exact key match (covers a dotted py/mod key too, e.g.
 * "pymergetic.metal.fs.open"). Joined summary/sig/body text, same
 * "\n\n"-between-parts convention as pmcmd.<name>.__doc__ / doc row
 * printing (util/doc.c's own pm_metal_doc_print).
 */
static mp_obj_t py_doc_help(mp_obj_t name_obj)
{
  const char          *name = mp_obj_str_get_str(name_obj);
  pm_metal_doc_view_t  view;
  char                 buf[400];
  size_t               off;
  int32_t              found = 0;

  if (pm_metal_doc_lookup(PM_METAL_DOC_SHELL, name, &view) == 0) {
    found = 1;
  } else {
    int32_t total = pm_metal_doc_count();
    int32_t i;

    for (i = 0; i < total; i++) {
      if (pm_metal_doc_at((uint32_t)i, &view) == 0 && strcmp(view.key, name) == 0) {
        found = 1;
        break;
      }
    }
  }

  if (!found) {
    return pm_metal_py_obj_none();
  }

  off = 0;
  if (view.summary[0] != '\0') {
    off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s", view.summary);
  }
  if (view.sig[0] != '\0') {
    off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s%s", off ? "\n\n" : "", view.sig);
  }
  if (view.body[0] != '\0') {
    off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s%s", off ? "\n\n" : "", view.body);
  }

  return pm_metal_py_str_new(buf);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_doc_help_obj, py_doc_help);
PM_METAL_PY_BIND_DOC(g_py_bind_doc_help,
                    "pymergetic.metal.doc",
                    "help",
                    py_doc_help_obj,
                    PM_METAL_PY_SYNC,
                    "Joined summary/sig/body text for one bare name, any kind.",
                    "help(name: str) -> str | None",
                    "Tries the shell face first (name typically a command), else scans the "
                    "whole catalog for an exact key match. None if nothing matches.");
