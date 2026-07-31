/*
 * Callable-docs registry (docs/DOC_IFACE_PLAN.md Part I) — read-only
 * adapter over three existing "base + face" homes, no second copy of any
 * text:
 *
 *   kind=shell — shell_cmd.h's live table (pm_metal_shell_cmd_count/at,
 *                so a guest register_cmd() row shows up here too)
 *   kind=py    — py.h's `.pm_metal_py_binds.*` linker section
 *                (mod.name -> summary/sig/body)
 *   kind=mod   — mod_lifecycle.h's register_func_doc records
 *                (mod.func -> summary/sig/body)
 *
 * Every pm_metal_doc_view_t/pm_metal_doc_lookup* call is host-only — the
 * three sources above are host memory in every case (shell_cmd_t rows are
 * host statics; py bind rows are a host linker section, this MicroPython
 * build is host-embedded, not a wasm guest, see py.h's file header;
 * mod_func_doc_* copies a guest's register_func_doc strings into a fixed
 * host buffer, see mod.c's mod_func_t) — there is never a guest pointer
 * to translate. A wasm mod that wants to read the catalog instead calls
 * the buffer-out imports declared in the `__wasm__` branch below: same
 * function names, a different (flattened, no pointer-holding struct
 * crosses the wasm boundary) signature, exactly why util/tar.h's
 * iter_name() takes (out, cap) instead of returning a `const char *` —
 * see that header's own file comment for the same reasoning.
 *
 * `doc_key` convention: "<kind>:<key>", kind one of "shell"/"py"/"mod",
 * split on the first ':' only (a py/mod key may itself contain dots,
 * e.g. "pymergetic.metal.fs.open").
 *
 * impl: common — src/pymergetic/metal/util/doc.c
 * impl: wasi import — src/pymergetic/metal/util/doc.c (wasm32 only)
 */
#ifndef PYMERGETIC_METAL_UTIL_DOC_H_
#define PYMERGETIC_METAL_UTIL_DOC_H_

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/wasi.h" /* IWYU pragma: keep */

/* This module's own import_module name — see doc.c's native_register()
 * for the host side that must build from this exact same constant. */
#define PM_METAL_UTIL_DOC_WASI_MODULE "pymergetic.metal.util.doc"

#if defined(__wasm__)
#define PM_METAL_UTIL_DOC_IMPORT(name) PM_METAL_WASI_IMPORT(PM_METAL_UTIL_DOC_WASI_MODULE, name)
#endif

/* "shell" / "py" / "mod" (doc_key prefix, doc.list(kind=...) argument). */
typedef enum {
  PM_METAL_DOC_SHELL = 1,
  PM_METAL_DOC_PY    = 2,
  PM_METAL_DOC_MOD   = 3
} pm_metal_doc_kind_t;

/* Longest formatted "%s%s%s" (summary + "\n\n" + sig + "\n\n" + body)
 * pm_metal_doc_print()/print_index() will ever build on the stack. */
#define PM_METAL_DOC_LINE_MAX 400U

#if !defined(__wasm__)

/*
 * Read-only view into one entry's own home — every pointer here is
 * borrowed (never freed by the caller), valid until the *next* call into
 * this module (see file header re: joined mod.func/mod.name keys living
 * in a small internal scratch buffer) — copy out anything you need to
 * keep past that.
 */
typedef struct {
  pm_metal_doc_kind_t kind;
  const char         *key;
  const char         *summary; /* never NULL; "" if that face set none */
  const char         *sig;     /* never NULL; "" if unset */
  const char         *body;    /* never NULL; "" if unset */
} pm_metal_doc_view_t;

/* Total entries across all three kinds right now (mod entries grow as
 * guests load and call register_func_doc). */
int32_t pm_metal_doc_count(void);

/* Fills *out with entry i (0..pm_metal_doc_count()-1), any kind, in
 * shell-then-py-then-mod order. 0 ok, -1 if i is out of range. */
int32_t pm_metal_doc_at(uint32_t i, pm_metal_doc_view_t *out);

/* Direct (kind, key) lookup — key is "mem" for shell, "mod.name" (dotted
 * bind path) for py, "modname.funcname" for mod. 0 ok, -1 unknown. */
int32_t pm_metal_doc_lookup(pm_metal_doc_kind_t kind, const char *key, pm_metal_doc_view_t *out);

/* Same, but kind+key packed as one "<kind>:<key>" string (see file
 * header). 0 ok, -1 malformed or unknown. */
int32_t pm_metal_doc_lookup_key(const char *doc_key, pm_metal_doc_view_t *out);

/* pm_metal_shell_out() lines: summary, then "usage: <sig>" if set, then
 * body if set — same shape shell_core_cmds.c's `help <name>` prints,
 * this is what backs it (and the `doc`/`iface` shell commands' own
 * detail view). Prints "no such doc: <kind>:<key>" if not found. */
void pm_metal_doc_print(pm_metal_doc_kind_t kind, const char *key);

/* One "<key>  <summary>" line per entry of that kind (or every kind if
 * kind is 0) — the catalog's own `help`-style listing. */
void pm_metal_doc_print_index(pm_metal_doc_kind_t kind);

/*
 * Registers this module's own wasi-style imports (see
 * PM_METAL_UTIL_DOC_WASI_MODULE above) — host-only, never included by a
 * mod. Call once, after wasm_runtime_full_init() has succeeded and
 * before the first load()/instantiate() of any module that might import
 * these (guest/wasm/wasm.c's pm_metal_wasm_init() is the caller).
 * Returns 0 on success, -1 if WAMR rejected the registration.
 */
int pm_metal_util_doc_native_register(void);

#else /* __wasm__ */

/* Buffer-out guest ABI (see file header) — cap 0 skips that field
 * entirely (no write, no truncation attempt), matching util/tar.h's own
 * iter_name() convention. out_kind mirrors pm_metal_doc_kind_t's values. */

extern int32_t pm_metal_doc_count(void) PM_METAL_UTIL_DOC_IMPORT(pm_metal_doc_count);

extern int32_t pm_metal_doc_at(uint32_t  i,
                               uint32_t *out_kind,
                               char     *key,
                               uint32_t  key_cap,
                               char     *summary,
                               uint32_t  summary_cap,
                               char     *sig,
                               uint32_t  sig_cap,
                               char     *body,
                               uint32_t  body_cap) PM_METAL_UTIL_DOC_IMPORT(pm_metal_doc_at);

extern int32_t pm_metal_doc_lookup(uint32_t    kind,
                                   const char *key,
                                   char       *summary,
                                   uint32_t    summary_cap,
                                   char       *sig,
                                   uint32_t    sig_cap,
                                   char       *body,
                                   uint32_t body_cap) PM_METAL_UTIL_DOC_IMPORT(pm_metal_doc_lookup);

extern int32_t pm_metal_doc_lookup_key(const char *doc_key,
                                       uint32_t   *out_kind,
                                       char       *key,
                                       uint32_t    key_cap,
                                       char       *summary,
                                       uint32_t    summary_cap,
                                       char       *sig,
                                       uint32_t    sig_cap,
                                       char       *body,
                                       uint32_t    body_cap)
  PM_METAL_UTIL_DOC_IMPORT(pm_metal_doc_lookup_key);

extern void pm_metal_doc_print(uint32_t kind, const char *key)
  PM_METAL_UTIL_DOC_IMPORT(pm_metal_doc_print);

extern void pm_metal_doc_print_index(uint32_t kind)
  PM_METAL_UTIL_DOC_IMPORT(pm_metal_doc_print_index);

#endif /* __wasm__ */

#endif /* PYMERGETIC_METAL_UTIL_DOC_H_ */
