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

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_PY_IMPORT(name) PM_METAL_WASI_IMPORT(PM_METAL_PY_WASI_MODULE, name)

/**
 * Start a Python script task. @a path is a guest pointer to a NUL-terminated
 * path (WAMR `$` string). Returns a Metal task handle (0 on failure).
 */
extern pm_metal_async_handle_t pm_metal_py_run_script(const char *path)
  PM_METAL_PY_IMPORT(pm_metal_py_run_script);
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
pm_metal_async_handle_t pm_metal_py_fn_call_async(pm_metal_py_fn_t *fn, uint32_t arg0);
int                     pm_metal_py_lookup(const char *dotted, pm_metal_py_ref_t *out);

int pm_metal_py_zip_ensure(void);
int pm_metal_py_native_register(void);

typedef struct pm_metal_py_bind {
  const char         *mod;
  const char         *name;
  void               *fn;
  pm_metal_py_class_t class_;
} pm_metal_py_bind_t;

int pm_metal_py_bind_table(const pm_metal_py_bind_t *rows, size_t n);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_PY_PY_H_ */
