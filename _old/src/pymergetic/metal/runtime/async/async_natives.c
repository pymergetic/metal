/** @file
  WAMR natives for pymergetic.metal.async.
**/
#include <pymergetic/metal/runtime/async/async.h>

#include <stdint.h>

#include "wasm_export.h"

static uint32_t pm_metal_async_coro_create_native(wasm_exec_env_t exec_env, uint32_t state_bytes)
{
  (void)exec_env;
  /* Guest import: step is session export; NULL selects guest path. */
  return pm_metal_async_coro_create(NULL, state_bytes);
}

static uint32_t pm_metal_async_coro_state_native(wasm_exec_env_t exec_env, uint32_t h)
{
  (void)exec_env;
  return (uint32_t)(uintptr_t)pm_metal_async_coro_state(h);
}

static uint32_t pm_metal_async_coro_alloc_native(wasm_exec_env_t exec_env, uint32_t h, uint32_t n)
{
  (void)exec_env;
  return (uint32_t)(uintptr_t)pm_metal_async_coro_alloc(h, n);
}

static void pm_metal_async_coro_close_native(wasm_exec_env_t exec_env, uint32_t h)
{
  (void)exec_env;
  pm_metal_async_coro_close(h);
}

static uint32_t pm_metal_async_sleep_native(wasm_exec_env_t exec_env, uint32_t ms)
{
  (void)exec_env;
  return pm_metal_async_sleep(ms);
}

static uint32_t pm_metal_async_sleep_us_native(wasm_exec_env_t exec_env, uint64_t us)
{
  (void)exec_env;
  return pm_metal_async_sleep_us(us);
}

static uint32_t pm_metal_async_sleep_until_us_native(wasm_exec_env_t exec_env, uint64_t deadline_us)
{
  (void)exec_env;
  return pm_metal_async_sleep_until_us(deadline_us);
}

static uint32_t pm_metal_async_present_native(wasm_exec_env_t exec_env, uint32_t surface)
{
  (void)exec_env;
  return pm_metal_async_present(surface);
}

static uint32_t pm_metal_async_frame_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_async_frame();
}

static uint32_t pm_metal_async_yield_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_async_yield();
}

static int32_t pm_metal_async_await_native(wasm_exec_env_t exec_env, uint32_t self_h, uint32_t aw_h)
{
  (void)exec_env;
  return pm_metal_async_await(self_h, aw_h);
}

static uint32_t pm_metal_async_create_task_native(wasm_exec_env_t exec_env, uint32_t coro_h)
{
  (void)exec_env;
  return pm_metal_async_create_task(coro_h);
}

static int32_t pm_metal_async_await_task_native(wasm_exec_env_t exec_env,
                                                uint32_t        self_h,
                                                uint32_t        task_h)
{
  (void)exec_env;
  return pm_metal_async_await_task(self_h, task_h);
}

static void pm_metal_async_task_cancel_native(wasm_exec_env_t exec_env, uint32_t task_h)
{
  (void)exec_env;
  pm_metal_async_task_cancel(task_h);
}

static int32_t pm_metal_async_task_status_native(wasm_exec_env_t exec_env, uint32_t task_h)
{
  (void)exec_env;
  return pm_metal_async_task_status(task_h);
}

static uint64_t pm_metal_async_mono_ms_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_async_mono_ms();
}

static uint64_t pm_metal_async_mono_us_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_async_mono_us();
}

static uint32_t pm_metal_async_result_u32_native(wasm_exec_env_t exec_env, uint32_t self_h)
{
  (void)exec_env;
  return pm_metal_async_result_u32(self_h);
}

static void pm_metal_async_set_result_u32_native(wasm_exec_env_t exec_env,
                                                 uint32_t        self_h,
                                                 uint32_t        v)
{
  (void)exec_env;
  pm_metal_async_set_result_u32(self_h, v);
}

static NativeSymbol g_pm_metal_async_native_symbols[] = {
  { "pm_metal_async_coro_create", (void *)pm_metal_async_coro_create_native, "(i)i", NULL },
  { "pm_metal_async_coro_state", (void *)pm_metal_async_coro_state_native, "(i)i", NULL },
  { "pm_metal_async_coro_alloc", (void *)pm_metal_async_coro_alloc_native, "(ii)i", NULL },
  { "pm_metal_async_coro_close", (void *)pm_metal_async_coro_close_native, "(i)", NULL },
  { "pm_metal_async_sleep", (void *)pm_metal_async_sleep_native, "(i)i", NULL },
  { "pm_metal_async_sleep_us", (void *)pm_metal_async_sleep_us_native, "(I)i", NULL },
  { "pm_metal_async_sleep_until_us", (void *)pm_metal_async_sleep_until_us_native, "(I)i", NULL },
  { "pm_metal_async_present", (void *)pm_metal_async_present_native, "(i)i", NULL },
  { "pm_metal_async_frame", (void *)pm_metal_async_frame_native, "()i", NULL },
  { "pm_metal_async_yield", (void *)pm_metal_async_yield_native, "()i", NULL },
  { "pm_metal_async_await", (void *)pm_metal_async_await_native, "(ii)i", NULL },
  { "pm_metal_async_create_task", (void *)pm_metal_async_create_task_native, "(i)i", NULL },
  { "pm_metal_async_await_task", (void *)pm_metal_async_await_task_native, "(ii)i", NULL },
  { "pm_metal_async_task_cancel", (void *)pm_metal_async_task_cancel_native, "(i)", NULL },
  { "pm_metal_async_task_status", (void *)pm_metal_async_task_status_native, "(i)i", NULL },
  { "pm_metal_async_mono_ms", (void *)pm_metal_async_mono_ms_native, "()I", NULL },
  { "pm_metal_async_mono_us", (void *)pm_metal_async_mono_us_native, "()I", NULL },
  { "pm_metal_async_result_u32", (void *)pm_metal_async_result_u32_native, "(i)i", NULL },
  { "pm_metal_async_set_result_u32", (void *)pm_metal_async_set_result_u32_native, "(ii)", NULL },
};

int pm_metal_async_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_ASYNC_WASI_MODULE,
                                     g_pm_metal_async_native_symbols,
                                     sizeof(g_pm_metal_async_native_symbols) /
                                       sizeof(g_pm_metal_async_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
