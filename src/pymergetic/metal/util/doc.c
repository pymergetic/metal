/** @file
  pm_metal_doc_* — impl: common (see util/doc.h; wasm32 mods reach this
  same code via this file's own wasi-style import registration at the
  bottom, not via a second compiled copy).

  No storage of its own: count()/at()/lookup() walk shell_cmd.c's live
  table, py_bind.c's `.pm_metal_py_binds.*` linker section, and mod.c's
  register_func_doc records, in that fixed order (shell, then py, then
  mod) for count()/at()'s flat index space.
**/
#include <pymergetic/metal/util/doc.h>

#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/guest/mod/mod_lifecycle.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/shell/shell_cmd.h>

extern const pm_metal_py_bind_t __pm_metal_py_binds_start[];
extern const pm_metal_py_bind_t __pm_metal_py_binds_end[];

static uint32_t PyBindCount(void)
{
  return (uint32_t)(__pm_metal_py_binds_end - __pm_metal_py_binds_start);
}

/*
 * Joined "mod.name" (py bind) / "mod.func" (mod doc) key — one shared
 * scratch buffer, valid only until the next call into this file (see
 * doc.h's pm_metal_doc_view_t comment); every caller today (shell `doc`/
 * `iface`, doc_py_bind.c) reads the view and is done with it before
 * calling back in, same convention shell_cmd.c's own line[]/namepad[]
 * formatting buffers rely on.
 */
static char s_key_scratch[192];

static const char *DocKeyJoin(const char *a, const char *b)
{
  snprintf(s_key_scratch, sizeof(s_key_scratch), "%s.%s", a, b);
  return s_key_scratch;
}

static const char *DocEmpty(const char *s)
{
  return (s != NULL) ? s : "";
}

static void ViewFromShell(const pm_metal_shell_cmd_t *cmd, pm_metal_doc_view_t *out)
{
  out->kind    = PM_METAL_DOC_SHELL;
  out->key     = cmd->name;
  out->summary = DocEmpty(cmd->help);
  out->sig     = DocEmpty(cmd->sig);
  out->body    = DocEmpty(cmd->body);
}

static void ViewFromPyBind(const pm_metal_py_bind_t *row, pm_metal_doc_view_t *out)
{
  out->kind    = PM_METAL_DOC_PY;
  out->key     = DocKeyJoin(row->mod, row->name);
  out->summary = DocEmpty(row->summary);
  out->sig     = DocEmpty(row->sig);
  out->body    = DocEmpty(row->body);
}

int32_t pm_metal_doc_count(void)
{
  return (int32_t)(pm_metal_shell_cmd_count() + PyBindCount() + pm_metal_mod_func_doc_count());
}

int32_t pm_metal_doc_at(uint32_t i, pm_metal_doc_view_t *out)
{
  uint32_t n_shell;
  uint32_t n_py;

  if (out == NULL) {
    return -1;
  }

  n_shell = pm_metal_shell_cmd_count();
  if (i < n_shell) {
    ViewFromShell(pm_metal_shell_cmd_at(i), out);
    return 0;
  }
  i -= n_shell;

  n_py = PyBindCount();
  if (i < n_py) {
    ViewFromPyBind(&__pm_metal_py_binds_start[i], out);
    return 0;
  }
  i -= n_py;

  {
    const char *mod_name;
    const char *func_name;
    const char *summary;
    const char *sig;
    const char *body;

    if (pm_metal_mod_func_doc_at(i, &mod_name, &func_name, &summary, &sig, &body) != 0) {
      return -1;
    }

    out->kind    = PM_METAL_DOC_MOD;
    out->key     = DocKeyJoin(mod_name, func_name);
    out->summary = DocEmpty(summary);
    out->sig     = DocEmpty(sig);
    out->body    = DocEmpty(body);
  }
  return 0;
}

static int32_t LookupShell(const char *key, pm_metal_doc_view_t *out)
{
  const pm_metal_shell_cmd_t *cmd = pm_metal_shell_cmd_find(key);

  if (cmd == NULL) {
    return -1;
  }

  ViewFromShell(cmd, out);
  return 0;
}

static int32_t LookupPy(const char *key, pm_metal_doc_view_t *out)
{
  uint32_t n = PyBindCount();
  uint32_t i;

  /*
   * pmcmd.<cmd> — Python face of the shell row (same string home as
   * kind=shell / console help; not a second bind). Listed by
   * doc.list("py") so the py filter shows every Python-callable surface.
   */
  if (key != NULL && strncmp(key, "pmcmd.", 6) == 0 && key[6] != '\0') {
    if (LookupShell(key + 6, out) != 0) {
      return -1;
    }
    out->kind = PM_METAL_DOC_PY;
    out->key  = DocKeyJoin("pmcmd", key + 6);
    return 0;
  }

  for (i = 0; i < n; i++) {
    const pm_metal_py_bind_t *row = &__pm_metal_py_binds_start[i];

    if (strcmp(DocKeyJoin(row->mod, row->name), key) == 0) {
      ViewFromPyBind(row, out);
      return 0;
    }
  }

  return -1;
}

static int32_t LookupMod(const char *key, pm_metal_doc_view_t *out)
{
  char        mod_name[64];
  const char *dot;
  const char *summary;
  const char *sig;
  const char *body;

  /* mod names never contain '.' (see mods/MODS.md naming) — first '.'
   * always splits mod from func, even for a dotted-looking func name. */
  dot = strchr(key, '.');
  if (dot == NULL || (size_t)(dot - key) >= sizeof(mod_name)) {
    return -1;
  }

  memcpy(mod_name, key, (size_t)(dot - key));
  mod_name[dot - key] = '\0';

  if (pm_metal_mod_func_doc_get(mod_name, dot + 1, &summary, &sig, &body) != 0) {
    return -1;
  }

  out->kind    = PM_METAL_DOC_MOD;
  out->key     = DocKeyJoin(mod_name, dot + 1);
  out->summary = DocEmpty(summary);
  out->sig     = DocEmpty(sig);
  out->body    = DocEmpty(body);
  return 0;
}

int32_t pm_metal_doc_lookup(pm_metal_doc_kind_t kind, const char *key, pm_metal_doc_view_t *out)
{
  if (key == NULL || out == NULL) {
    return -1;
  }

  switch (kind) {
  case PM_METAL_DOC_SHELL:
    return LookupShell(key, out);
  case PM_METAL_DOC_PY:
    return LookupPy(key, out);
  case PM_METAL_DOC_MOD:
    return LookupMod(key, out);
  default:
    return -1;
  }
}

static int32_t KindFromName(const char *name, size_t len, pm_metal_doc_kind_t *out)
{
  if (len == 5 && strncmp(name, "shell", 5) == 0) {
    *out = PM_METAL_DOC_SHELL;
    return 0;
  }
  if (len == 2 && strncmp(name, "py", 2) == 0) {
    *out = PM_METAL_DOC_PY;
    return 0;
  }
  if (len == 3 && strncmp(name, "mod", 3) == 0) {
    *out = PM_METAL_DOC_MOD;
    return 0;
  }
  return -1;
}

int32_t pm_metal_doc_lookup_key(const char *doc_key, pm_metal_doc_view_t *out)
{
  const char         *colon;
  pm_metal_doc_kind_t kind;

  if (doc_key == NULL || out == NULL) {
    return -1;
  }

  colon = strchr(doc_key, ':');
  if (colon == NULL) {
    return -1;
  }

  if (KindFromName(doc_key, (size_t)(colon - doc_key), &kind) != 0) {
    return -1;
  }

  return pm_metal_doc_lookup(kind, colon + 1, out);
}

void pm_metal_doc_print(pm_metal_doc_kind_t kind, const char *key)
{
  pm_metal_doc_view_t view;
  char                line[PM_METAL_DOC_LINE_MAX];

  if (pm_metal_doc_lookup(kind, key, &view) != 0) {
    snprintf(line, sizeof(line), "no such doc: %s", key != NULL ? key : "(null)");
    pm_metal_shell_out(line);
    return;
  }

  snprintf(line, sizeof(line), "%s: %s", view.key, view.summary[0] != '\0' ? view.summary : "-");
  pm_metal_shell_out(line);

  if (view.sig[0] != '\0') {
    snprintf(line, sizeof(line), "  usage: %s", view.sig);
    pm_metal_shell_out(line);
  }
  if (view.body[0] != '\0') {
    pm_metal_shell_out_lines(view.body);
  }
}

void pm_metal_doc_print_index(pm_metal_doc_kind_t kind)
{
  int32_t n = pm_metal_doc_count();
  int32_t i;

  for (i = 0; i < n; i++) {
    pm_metal_doc_view_t view;

    if (pm_metal_doc_at((uint32_t)i, &view) != 0) {
      continue;
    }
    if (kind != 0 && view.kind != kind) {
      continue;
    }

    {
      char line[PM_METAL_DOC_LINE_MAX];

      snprintf(line, sizeof(line), "  %-28s %s", view.key, view.summary);
      pm_metal_shell_out(line);
    }
  }
}

/*
 * wasi-style import bridge — buffer-out flattening (doc.h's file header
 * explains why): each wrapper copies at most one view's worth of
 * already-NUL-terminated host strings into guest-owned buffers, exactly
 * like util/tar.c's iter_name() bridge. WAMR signature letters: '*'+'~'
 * pairs = bounds-checked (pointer, cap) pairs; '$' = NUL-terminated
 * guest string; see wasm_export.h.
 */
#include "wasm_export.h"

static void CopyOut(const char *src, char *dst, uint32_t cap)
{
  if (dst == NULL || cap == 0) {
    return;
  }
  snprintf(dst, cap, "%s", src != NULL ? src : "");
}

static int32_t pm_metal_doc_count_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_doc_count();
}

static int32_t pm_metal_doc_at_native(wasm_exec_env_t exec_env,
                                      uint32_t        i,
                                      uint32_t       *out_kind,
                                      char           *key,
                                      uint32_t        key_cap,
                                      char           *summary,
                                      uint32_t        summary_cap,
                                      char           *sig,
                                      uint32_t        sig_cap,
                                      char           *body,
                                      uint32_t        body_cap)
{
  pm_metal_doc_view_t view;

  (void)exec_env;
  if (pm_metal_doc_at(i, &view) != 0) {
    return -1;
  }

  if (out_kind != NULL) {
    *out_kind = (uint32_t)view.kind;
  }
  CopyOut(view.key, key, key_cap);
  CopyOut(view.summary, summary, summary_cap);
  CopyOut(view.sig, sig, sig_cap);
  CopyOut(view.body, body, body_cap);
  return 0;
}

static int32_t pm_metal_doc_lookup_native(wasm_exec_env_t exec_env,
                                          uint32_t        kind,
                                          const char     *key,
                                          char           *summary,
                                          uint32_t        summary_cap,
                                          char           *sig,
                                          uint32_t        sig_cap,
                                          char           *body,
                                          uint32_t        body_cap)
{
  pm_metal_doc_view_t view;

  (void)exec_env;
  if (pm_metal_doc_lookup((pm_metal_doc_kind_t)kind, key, &view) != 0) {
    return -1;
  }

  CopyOut(view.summary, summary, summary_cap);
  CopyOut(view.sig, sig, sig_cap);
  CopyOut(view.body, body, body_cap);
  return 0;
}

static int32_t pm_metal_doc_lookup_key_native(wasm_exec_env_t exec_env,
                                              const char     *doc_key,
                                              uint32_t       *out_kind,
                                              char           *key,
                                              uint32_t        key_cap,
                                              char           *summary,
                                              uint32_t        summary_cap,
                                              char           *sig,
                                              uint32_t        sig_cap,
                                              char           *body,
                                              uint32_t        body_cap)
{
  pm_metal_doc_view_t view;

  (void)exec_env;
  if (pm_metal_doc_lookup_key(doc_key, &view) != 0) {
    return -1;
  }

  if (out_kind != NULL) {
    *out_kind = (uint32_t)view.kind;
  }
  CopyOut(view.key, key, key_cap);
  CopyOut(view.summary, summary, summary_cap);
  CopyOut(view.sig, sig, sig_cap);
  CopyOut(view.body, body, body_cap);
  return 0;
}

static void pm_metal_doc_print_native(wasm_exec_env_t exec_env, uint32_t kind, const char *key)
{
  (void)exec_env;
  pm_metal_doc_print((pm_metal_doc_kind_t)kind, key);
}

static void pm_metal_doc_print_index_native(wasm_exec_env_t exec_env, uint32_t kind)
{
  (void)exec_env;
  pm_metal_doc_print_index((pm_metal_doc_kind_t)kind);
}

static NativeSymbol g_pm_metal_util_doc_native_symbols[] = {
  { "pm_metal_doc_count", (void *)pm_metal_doc_count_native, "()i", NULL },
  { "pm_metal_doc_at", (void *)pm_metal_doc_at_native, "(i**~*~*~*~)i", NULL },
  { "pm_metal_doc_lookup", (void *)pm_metal_doc_lookup_native, "(i$*~*~*~)i", NULL },
  { "pm_metal_doc_lookup_key", (void *)pm_metal_doc_lookup_key_native, "($**~*~*~*~)i", NULL },
  { "pm_metal_doc_print", (void *)pm_metal_doc_print_native, "(i$)", NULL },
  { "pm_metal_doc_print_index", (void *)pm_metal_doc_print_index_native, "(i)", NULL },
};

int pm_metal_util_doc_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_UTIL_DOC_WASI_MODULE,
                                     g_pm_metal_util_doc_native_symbols,
                                     sizeof(g_pm_metal_util_doc_native_symbols) /
                                       sizeof(g_pm_metal_util_doc_native_symbols[0]))) {
    return -1;
  }
  return 0;
}
