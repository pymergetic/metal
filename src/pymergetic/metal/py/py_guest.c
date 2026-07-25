/** @file Guest dual-ABI pymergetic.metal.py (start script + fn resolve/call). */
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/runtime/async/async.h>

#if !defined(__wasm__)
#include <string.h>

#include "wasm_export.h"

static int32_t py_run_script_native(wasm_exec_env_t env, const char *path)
{
  wasm_module_inst_t      inst;
  pm_metal_async_handle_t h;

  inst = wasm_runtime_get_module_inst(env);
  if (path == NULL || inst == NULL || !wasm_runtime_validate_native_addr(inst, (void *)path, 1)) {
    return 0;
  }
  h = pm_metal_py_run_script(path);
  return (int32_t)h;
}

static int32_t py_fn_resolve_native(wasm_exec_env_t env, const char *dotted_name)
{
  wasm_module_inst_t inst;

  inst = wasm_runtime_get_module_inst(env);
  if (dotted_name == NULL || inst == NULL
      || !wasm_runtime_validate_native_addr(inst, (void *)dotted_name, 1)) {
    return (int32_t)PM_METAL_PY_FN_H_INVALID;
  }
  return (int32_t)pm_metal_py_fn_resolve(dotted_name);
}

/*
 * out_dest is a guest linear-memory offset (uint32_t), not a translated
 * pointer — py.h's pm_metal_py_fn_call() dual-ABI decl documents why:
 * WAMR's automatic '*'/'~' marshaling only bounds-checks 1 byte without
 * an explicit accompanying length arg, so this manually validates + writes
 * exactly sizeof(int32_t), same pattern as pm_metal_process_info_native.
 */
static int32_t
py_fn_call_native(wasm_exec_env_t env, uint32_t fn_h, uint32_t out_dest, int32_t a, int32_t b)
{
  wasm_module_inst_t inst;
  int32_t            out_i32 = 0;
  int32_t            rc;
  void              *native;

  inst = wasm_runtime_get_module_inst(env);
  if (inst == NULL || !wasm_runtime_validate_app_addr(inst, out_dest, sizeof(out_i32))) {
    return -1;
  }

  rc = pm_metal_py_fn_call((pm_metal_py_fn_h_t)fn_h, &out_i32, a, b);
  if (rc != 0) {
    return rc;
  }

  native = wasm_runtime_addr_app_to_native(inst, out_dest);
  if (native == NULL) {
    return -1;
  }
  memcpy(native, &out_i32, sizeof(out_i32));
  return 0;
}

static int32_t py_fn_call_async_native(wasm_exec_env_t env, uint32_t fn_h, uint32_t arg0)
{
  (void)env;
  return (int32_t)pm_metal_py_fn_call_async((pm_metal_py_fn_h_t)fn_h, arg0);
}

static NativeSymbol g_py_symbols[] = {
  { "pm_metal_py_run_script", (void *)py_run_script_native, "($)i", NULL },
  { "pm_metal_py_fn_resolve", (void *)py_fn_resolve_native, "($)i", NULL },
  { "pm_metal_py_fn_call", (void *)py_fn_call_native, "(iiii)i", NULL },
  { "pm_metal_py_fn_call_async", (void *)py_fn_call_async_native, "(ii)i", NULL },
};

int pm_metal_py_native_register(void)
{
  return wasm_runtime_register_natives(
           PM_METAL_PY_WASI_MODULE, g_py_symbols, sizeof(g_py_symbols) / sizeof(g_py_symbols[0]))
           ? 0
           : -1;
}
#endif
