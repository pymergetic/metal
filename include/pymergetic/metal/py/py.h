/*
 * Metal MicroPython — port-neutral host/guest dual ABI.
 * EFI/BIOS only link sources; all logic lives in src/pymergetic/metal/py/.
 *
 * Design: docs/MICROPYTHON.md
 */
#ifndef PYMERGETIC_METAL_PY_PY_H_
#define PYMERGETIC_METAL_PY_PY_H_

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PM_METAL_PY_BLOB_BYTES
#define PM_METAL_PY_BLOB_BYTES (256u * 1024u)
#endif

#define PM_METAL_PY_WASI_MODULE "pymergetic.metal.py"

/**
 * Opaque handle onto a resolved Python callable (pm_metal_py_fn_resolve),
 * shared shape guest and host — mirrors pm_metal_mod_fn_h_t (mod.h): a
 * wasm guest never sees the underlying mp_obj_t, only this index, so a
 * guest can drive a specific bound Python function the same "resolve
 * once, call by handle" way Python drives a specific mod function via
 * pymergetic.metal.mod (guest/mod/mod_py_bind.c) — see docs/MICROPYTHON.md.
 */
typedef uint32_t pm_metal_py_fn_h_t;
#define PM_METAL_PY_FN_H_INVALID 0u
#define PM_METAL_PY_FN_H_MAX     512u

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_PY_IMPORT(name) PM_METAL_WASI_IMPORT(PM_METAL_PY_WASI_MODULE, name)

/**
 * Start a Python script task. @a path is a guest pointer to a NUL-terminated
 * path (WAMR `$` string). Returns a Metal task handle (0 on failure).
 */
extern pm_metal_async_handle_t pm_metal_py_run_script(const char *path)
  PM_METAL_PY_IMPORT(pm_metal_py_run_script);

/**
 * Resolve a dotted Python name ("pymergetic.metal.aio.mono_us", a
 * user script's top-level "add", ...) to a handle. 0 = fail.
 */
extern pm_metal_py_fn_h_t pm_metal_py_fn_resolve(const char *dotted_name)
  PM_METAL_PY_IMPORT(pm_metal_py_fn_resolve);

/**
 * Sync call: target must be a plain (non async-def) callable taking
 * (int, int). @a out_dest is a guest linear-memory offset to one
 * int32_t slot (bounds-checked + written host-side, like
 * pm_metal_process_info's `dest`, not WAMR's generic `*~` since that
 * only auto-checks 1 byte without an explicit length arg). 0 ok, -1
 * fail (bad handle, lock contention, sync-into-async mismatch, or
 * exception).
 */
extern int32_t pm_metal_py_fn_call(pm_metal_py_fn_h_t fn_h, uint32_t out_dest, int32_t a, int32_t b)
  PM_METAL_PY_IMPORT(pm_metal_py_fn_call);

/**
 * Async call: target must be an async-def callable taking one int
 * arg. Spawns a Metal task (await via pm_metal_async_await_task /
 * poll via pm_metal_async_task_status, both already guest-visible in
 * async.h) — 0 on failure to spawn.
 */
extern pm_metal_async_handle_t pm_metal_py_fn_call_async(pm_metal_py_fn_h_t fn_h, uint32_t arg0)
  PM_METAL_PY_IMPORT(pm_metal_py_fn_call_async);
#else
int    pm_metal_py_init(void);
int    pm_metal_py_ready(void);
size_t pm_metal_py_blob_bytes(void);

/** New Metal/Python task on the always-on blob; returns task handle or 0. */
pm_metal_async_handle_t pm_metal_py_run_script(const char *path);
pm_metal_async_handle_t pm_metal_py_run_str(const char *src);

typedef enum {
  PM_METAL_PY_SYNC   = 1,
  PM_METAL_PY_ASYNC  = 2,
  PM_METAL_PY_FACADE = 3
} pm_metal_py_class_t;

typedef struct pm_metal_py_ref {
  void *obj;
} pm_metal_py_ref_t;

typedef struct pm_metal_py_fn {
  pm_metal_py_ref_t ref;
  uint8_t           class_;
  char              name[64];
} pm_metal_py_fn_t;

int pm_metal_py_fn_bind(pm_metal_py_fn_t *fn, const char *dotted_name);
int pm_metal_py_call(pm_metal_py_fn_t *fn, int32_t *out_i32, int32_t a, int32_t b);
/* Already-bound pointer variant (py_shell.c's `py` command) — kept
 * distinct from the handle-based pm_metal_py_fn_call_async below since a
 * host callsite that already holds the pm_metal_py_fn_t from fn_bind has
 * no reason to also burn a handle-table slot. */
pm_metal_async_handle_t pm_metal_py_fn_call_async_bound(pm_metal_py_fn_t *fn, uint32_t arg0);
int                     pm_metal_py_lookup(const char *dotted, pm_metal_py_ref_t *out);

/**
 * Handle-table trio — dual-ABI with the wasm declarations above, so a
 * guest and the host drive a resolved Python callable the exact same
 * "resolve once, call by handle" way. Host callers that don't need
 * cross-ABI handles can use pm_metal_py_fn_bind + pm_metal_py_call /
 * pm_metal_py_fn_call_async_bound directly instead.
 */
pm_metal_py_fn_h_t      pm_metal_py_fn_resolve(const char *dotted_name);
int                     pm_metal_py_fn_call(pm_metal_py_fn_h_t fn_h, int32_t *out_i32, int32_t a, int32_t b);
pm_metal_async_handle_t pm_metal_py_fn_call_async(pm_metal_py_fn_h_t fn_h, uint32_t arg0);

int pm_metal_py_zip_ensure(void);
int pm_metal_py_native_register(void);

typedef struct pm_metal_py_bind {
  const char         *mod;
  const char         *name;
  void               *fn;
  pm_metal_py_class_t class_;
} pm_metal_py_bind_t;

int pm_metal_py_bind_table(const pm_metal_py_bind_t *rows, size_t n);

/**
 * Place one C→Python bind in the auto-register section `.pm_metal_py_binds.*`
 * (mirrors PM_METAL_SHELL_CMD's linker-section self-registration).
 * @a fn_obj must already be a compile-time MicroPython callable — the
 * result of MP_DEFINE_CONST_FUN_OBJ_N — not a raw C function pointer;
 * pm_metal_py_binds_install() (called once from pm_metal_py_init) just
 * wires it into <mod_str>.<name_str> as a real dict attribute, so Python
 * call sites do mod.name(...) directly — never a string-keyed dispatch.
 * `var` must be a unique static identifier in the translation unit.
 */
#define PM_METAL_PY_BIND(var, mod_str, name_str, fn_obj, class_)                          \
  static const pm_metal_py_bind_t var                                                    \
    __attribute__((used, section(".pm_metal_py_binds.1"), aligned(16))) = {               \
      (mod_str), (name_str), (void *)&(fn_obj), (class_)                                  \
    }

/** Gather linker-section bind rows (called once from pm_metal_py_init). */
void pm_metal_py_binds_install(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_PY_PY_H_ */
