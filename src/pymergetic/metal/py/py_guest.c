/** @file Guest dual-ABI pymergetic.metal.py (start script). */
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/runtime/async/async.h>

#if !defined(__wasm__)
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

static NativeSymbol g_py_symbols[] = {
  { "pm_metal_py_run_script", (void *)py_run_script_native, "($)i", NULL },
};

int pm_metal_py_native_register(void)
{
  return wasm_runtime_register_natives(
           PM_METAL_PY_WASI_MODULE, g_py_symbols, sizeof(g_py_symbols) / sizeof(g_py_symbols[0]))
           ? 0
           : -1;
}
#endif
