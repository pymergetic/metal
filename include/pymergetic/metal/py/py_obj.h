/*
 * Metal MicroPython — host-only value facade.
 *
 * Lets C code outside py/ (async/process/mod/shell bind files) build and
 * read Python values without pulling in MicroPython's own headers
 * (py/obj.h, py/runtime.h, ...) — only py/ itself (py_obj.c, py_await.c)
 * includes those. pm_metal_py_obj_t is binary-compatible with mp_obj_t on
 * this port (MICROPY_OBJ_REPR_A: both are plain object pointers), but
 * callers should treat it as opaque.
 *
 * Host-only: a wasm guest never sees a Python object, only opaque numeric
 * handles (see py.h's guest branch) — no __wasm__ branch here on purpose.
 *
 * impl: src/pymergetic/metal/py/py_obj.c, src/pymergetic/metal/py/py_await.c
 */
#ifndef PYMERGETIC_METAL_PY_PY_OBJ_H_
#define PYMERGETIC_METAL_PY_PY_OBJ_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *pm_metal_py_obj_t;

pm_metal_py_obj_t pm_metal_py_obj_none(void);
bool              pm_metal_py_obj_is_none(pm_metal_py_obj_t o);

pm_metal_py_obj_t pm_metal_py_int_new(int64_t v);
int64_t           pm_metal_py_int_get(pm_metal_py_obj_t o);

pm_metal_py_obj_t pm_metal_py_str_new(const char *s);

/**
 * Stringify any object (int, float, custom __str__, an already-a-str, ...)
 * into buf/cap — the same conversion `str(x)` does. Returns 0 ok, -1 if it
 * didn't fit (buf still NUL-terminated, truncated).
 */
int pm_metal_py_obj_to_str(pm_metal_py_obj_t o, char *buf, size_t cap);

pm_metal_py_obj_t pm_metal_py_dict_new(size_t n_hint);
void pm_metal_py_dict_set_str(pm_metal_py_obj_t d, const char *key, pm_metal_py_obj_t val);

pm_metal_py_obj_t pm_metal_py_tuple_new(size_t n, const pm_metal_py_obj_t *items);

/**
 * Resolve (creating as needed) the module object for a dotted path —
 * wires mp_loaded_modules_dict + parent->child attributes at every level,
 * same helper PM_METAL_PY_BIND itself uses (py_bind.c), exposed here so a
 * C-side install helper outside py/ (e.g. mod_py_bind.c's
 * pymergetic.metal.mod) can attach its own non-callable singleton object
 * under a dotted path the same way. NULL on empty/malformed path.
 */
pm_metal_py_obj_t pm_metal_py_bind_resolve_module(const char *dotted);

/** Raise into the caller's own nlr_push scope — never returns. */
void pm_metal_py_raise_type_error(const char *msg);
void pm_metal_py_raise_value_error(const char *msg);
void pm_metal_py_raise_runtime_error(const char *msg);

/**
 * Awaitable bridge — turn a Metal async handle into a Python awaitable
 * object that parks the current Python task step
 * (pm_metal_py_job_current/_set_pending, py/ internal) until the handle
 * completes, then resumes the `await` expression. Two shapes, matching
 * every current + planned caller:
 *   - pm_metal_py_new_awaitable: resumes with None (pymergetic.metal.aio's
 *     sleep_us/yield_ — the caller only cares that it's done).
 *   - pm_metal_py_new_awaitable_u32: resumes with
 *     pm_metal_async_result_u32(h) (pymergetic.metal.mod's per-function
 *     awaitables — the mod's guest-side result payload).
 */
pm_metal_py_obj_t pm_metal_py_new_awaitable(pm_metal_async_handle_t h);
pm_metal_py_obj_t pm_metal_py_new_awaitable_u32(pm_metal_async_handle_t h);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_PY_PY_OBJ_H_ */
