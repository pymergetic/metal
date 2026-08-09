/*
 * Host (kernel) natives for guest_surface modules that have product faces.
 * Import module strings must match forge faces (stem path).
 *
 * Registers: log, async.{time,await,handle,task,coro}, mem cookies, fs.
 * Deferred until product hubs exist: process, audio, gfx, input, shell.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/async/await.h>
#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/runner.h>
#include <pymergetic/metal/async/time.h>
#include <pymergetic/metal/fs/__init__.h>
#include <pymergetic/metal/log/__init__.h>
#include <pymergetic/metal/mem/__init__.h>

#include "guest_coro.h"
#include "wasm_export.h"

/* ---- log ---- */

static void native_pm_metal_log(wasm_exec_env_t exec_env, char *line)
{
  (void)exec_env;
  if (line == NULL) {
    return;
  }
  pm_metal_log((const uint8_t *)line);
}

static NativeSymbol g_log_syms[] = {
    {"pm_metal_log", (void *)native_pm_metal_log, "($)", NULL},
};

/* ---- async.time ---- */

static uint64_t native_async_mono_us(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_async_mono_us();
}

static uint64_t native_async_mono_ms(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_async_mono_ms();
}

static uint32_t native_async_sleep_us(wasm_exec_env_t exec_env, uint64_t us)
{
  (void)exec_env;
  return pm_metal_async_sleep_us(us);
}

static uint32_t native_async_sleep_until_us(wasm_exec_env_t exec_env, uint64_t deadline_us)
{
  (void)exec_env;
  return pm_metal_async_sleep_until_us(deadline_us);
}

static uint32_t native_async_sleep(wasm_exec_env_t exec_env, uint32_t ms)
{
  (void)exec_env;
  return pm_metal_async_sleep(ms);
}

static uint32_t native_async_yield(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_async_yield();
}

static NativeSymbol g_async_time_syms[] = {
    {"pm_metal_async_mono_us", (void *)native_async_mono_us, "()I", NULL},
    {"pm_metal_async_mono_ms", (void *)native_async_mono_ms, "()I", NULL},
    {"pm_metal_async_sleep_us", (void *)native_async_sleep_us, "(I)i", NULL},
    {"pm_metal_async_sleep_until_us", (void *)native_async_sleep_until_us, "(I)i", NULL},
    {"pm_metal_async_sleep", (void *)native_async_sleep, "(i)i", NULL},
    {"pm_metal_async_yield", (void *)native_async_yield, "()i", NULL},
};

/* ---- async.await / handle / task ---- */

static int32_t native_async_await(wasm_exec_env_t exec_env, uint32_t self_h, uint32_t child_h)
{
  (void)exec_env;
  return (int32_t)pm_metal_async_await(self_h, child_h);
}

static uint32_t native_async_result_u32(wasm_exec_env_t exec_env, uint32_t h)
{
  (void)exec_env;
  return pm_metal_async_result_u32(h);
}

static void native_async_set_result_u32(wasm_exec_env_t exec_env, uint32_t h, uint32_t v)
{
  (void)exec_env;
  pm_metal_async_set_result_u32(h, v);
}

static int32_t native_async_status(wasm_exec_env_t exec_env, uint32_t h)
{
  (void)exec_env;
  return (int32_t)pm_metal_async_status(h);
}

static uint32_t native_async_create_task(wasm_exec_env_t exec_env, uint32_t h)
{
  (void)exec_env;
  return pm_metal_async_create_task(h);
}

static NativeSymbol g_async_await_syms[] = {
    {"pm_metal_async_await", (void *)native_async_await, "(ii)i", NULL},
};

static NativeSymbol g_async_handle_syms[] = {
    {"pm_metal_async_result_u32", (void *)native_async_result_u32, "(i)i", NULL},
    {"pm_metal_async_set_result_u32", (void *)native_async_set_result_u32, "(ii)", NULL},
    {"pm_metal_async_status", (void *)native_async_status, "(i)i", NULL},
};

static NativeSymbol g_async_task_syms[] = {
    {"pm_metal_async_create_task", (void *)native_async_create_task, "(i)i", NULL},
};

/* ---- async.coro ---- */

static uint32_t native_coro_create(wasm_exec_env_t exec_env, uint32_t state_bytes)
{
  return pm_metal_wasm_guest_coro_create(exec_env, state_bytes);
}

static uint32_t native_coro_state(wasm_exec_env_t exec_env, uint32_t h)
{
  (void)exec_env;
  return pm_metal_wasm_guest_coro_state(h);
}

static uint32_t native_coro_alloc(wasm_exec_env_t exec_env, uint32_t h, uint32_t n)
{
  (void)exec_env;
  return pm_metal_wasm_guest_coro_alloc(h, n);
}

static void native_coro_close(wasm_exec_env_t exec_env, uint32_t h)
{
  (void)exec_env;
  pm_metal_wasm_guest_coro_close(h);
}

static int32_t native_coro_smoke(wasm_exec_env_t exec_env)
{
  return pm_metal_wasm_guest_coro_smoke(exec_env);
}

static NativeSymbol g_async_coro_syms[] = {
    {"pm_metal_async_coro_create", (void *)native_coro_create, "(i)i", NULL},
    {"pm_metal_async_coro_state", (void *)native_coro_state, "(i)i", NULL},
    {"pm_metal_async_coro_alloc", (void *)native_coro_alloc, "(ii)i", NULL},
    {"pm_metal_async_coro_close", (void *)native_coro_close, "(i)", NULL},
    {"pm_metal_async_guest_coro_smoke", (void *)native_coro_smoke, "()i", NULL},
};

/* ---- mem cookies ---- */

static int32_t mem_linear(wasm_exec_env_t exec_env, uint32_t off, uint32_t n, void **native_out)
{
  wasm_module_inst_t inst;
  void *native;

  *native_out = NULL;
  if (exec_env == NULL || n == 0u) {
    return -1;
  }
  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL) {
    return -1;
  }
  if (!wasm_runtime_validate_app_addr(inst, (uint64_t)off, n)) {
    return -1;
  }
  native = wasm_runtime_addr_app_to_native(inst, (uint64_t)off);
  if (native == NULL) {
    return -1;
  }
  *native_out = native;
  return 0;
}

static uint32_t native_mem_alloc(wasm_exec_env_t exec_env, uint32_t size)
{
  (void)exec_env;
  return pm_metal_mem_guest_alloc(size);
}

static void native_mem_free(wasm_exec_env_t exec_env, uint32_t cookie)
{
  (void)exec_env;
  pm_metal_mem_guest_free(cookie);
}

static int32_t native_mem_copy_out(wasm_exec_env_t exec_env, uint32_t src_cookie, uint32_t dest_off,
                                   uint32_t n)
{
  uint8_t *p;
  void *linear;
  uint32_t cap;

  p = pm_metal_mem_guest_ptr(src_cookie);
  cap = pm_metal_mem_guest_size(src_cookie);
  if (p == NULL || n == 0u || n > cap) {
    return -1;
  }
  if (mem_linear(exec_env, dest_off, n, &linear) != 0) {
    return -1;
  }
  memcpy(linear, p, (size_t)n);
  return 0;
}

static int32_t native_mem_copy_in(wasm_exec_env_t exec_env, uint32_t dest_cookie, uint32_t src_off,
                                  uint32_t n)
{
  uint8_t *p;
  void *linear;
  uint32_t cap;

  p = pm_metal_mem_guest_ptr(dest_cookie);
  cap = pm_metal_mem_guest_size(dest_cookie);
  if (p == NULL || n == 0u || n > cap) {
    return -1;
  }
  if (mem_linear(exec_env, src_off, n, &linear) != 0) {
    return -1;
  }
  memcpy(p, linear, (size_t)n);
  return 0;
}

static int32_t native_mem_copy_out_at(wasm_exec_env_t exec_env, uint32_t src_cookie,
                                      uint32_t src_off, uint32_t dest_off, uint32_t n)
{
  uint8_t *p;
  void *linear;
  uint32_t cap;

  p = pm_metal_mem_guest_ptr(src_cookie);
  cap = pm_metal_mem_guest_size(src_cookie);
  if (p == NULL || n == 0u) {
    return -1;
  }
  if (src_off > cap || n > (cap - src_off)) {
    return -1;
  }
  if (mem_linear(exec_env, dest_off, n, &linear) != 0) {
    return -1;
  }
  memcpy(linear, p + src_off, (size_t)n);
  return 0;
}

static NativeSymbol g_mem_syms[] = {
    {"pm_metal_mem_alloc", (void *)native_mem_alloc, "(i)i", NULL},
    {"pm_metal_mem_free", (void *)native_mem_free, "(i)", NULL},
    {"pm_metal_mem_copy_out", (void *)native_mem_copy_out, "(iii)i", NULL},
    {"pm_metal_mem_copy_in", (void *)native_mem_copy_in, "(iii)i", NULL},
    {"pm_metal_mem_copy_out_at", (void *)native_mem_copy_out_at, "(iiii)i", NULL},
};

/* ---- fs ---- */

static uint32_t native_fs_size_async(wasm_exec_env_t exec_env, char *path)
{
  (void)exec_env;
  if (path == NULL) {
    return 0u;
  }
  return pm_metal_fs_size_async((const uint8_t *)path);
}

static uint32_t native_fs_mkdir_async(wasm_exec_env_t exec_env, char *path)
{
  (void)exec_env;
  if (path == NULL) {
    return 0u;
  }
  return pm_metal_fs_mkdir_async((const uint8_t *)path);
}

static uint32_t native_fs_result(wasm_exec_env_t exec_env, uint32_t h)
{
  (void)exec_env;
  return pm_metal_fs_result(h);
}

static uint32_t native_fs_read_mem_async(wasm_exec_env_t exec_env, char *path, uint32_t dest_cookie,
                                         uint32_t dest_len)
{
  (void)exec_env;
  if (path == NULL) {
    return 0u;
  }
  return pm_metal_fs_read_mem_async((const uint8_t *)path, dest_cookie, dest_len);
}

static uint32_t native_fs_write_mem_async(wasm_exec_env_t exec_env, char *path, uint32_t src_cookie,
                                          uint32_t src_len)
{
  (void)exec_env;
  if (path == NULL) {
    return 0u;
  }
  return pm_metal_fs_write_mem_async((const uint8_t *)path, src_cookie, src_len);
}

static NativeSymbol g_fs_syms[] = {
    {"pm_metal_fs_size_async", (void *)native_fs_size_async, "($)i", NULL},
    {"pm_metal_fs_mkdir_async", (void *)native_fs_mkdir_async, "($)i", NULL},
    {"pm_metal_fs_result", (void *)native_fs_result, "(i)i", NULL},
    {"pm_metal_fs_read_mem_async", (void *)native_fs_read_mem_async, "($ii)i", NULL},
    {"pm_metal_fs_write_mem_async", (void *)native_fs_write_mem_async, "($ii)i", NULL},
};

int32_t pm_metal_wasm_port_register_wasi_stubs(void);

int32_t pm_metal_wasm_port_register_host_natives(void)
{
  if (!wasm_runtime_register_natives("pymergetic.metal.log", g_log_syms,
                                     (uint32_t)(sizeof(g_log_syms) / sizeof(g_log_syms[0])))) {
    return -1;
  }
  if (!wasm_runtime_register_natives("pymergetic.metal.async.time", g_async_time_syms,
                                     (uint32_t)(sizeof(g_async_time_syms) /
                                                sizeof(g_async_time_syms[0])))) {
    return -1;
  }
  if (!wasm_runtime_register_natives("pymergetic.metal.async.await", g_async_await_syms,
                                     (uint32_t)(sizeof(g_async_await_syms) /
                                                sizeof(g_async_await_syms[0])))) {
    return -1;
  }
  if (!wasm_runtime_register_natives("pymergetic.metal.async.handle", g_async_handle_syms,
                                     (uint32_t)(sizeof(g_async_handle_syms) /
                                                sizeof(g_async_handle_syms[0])))) {
    return -1;
  }
  if (!wasm_runtime_register_natives("pymergetic.metal.async.task", g_async_task_syms,
                                     (uint32_t)(sizeof(g_async_task_syms) /
                                                sizeof(g_async_task_syms[0])))) {
    return -1;
  }
  if (!wasm_runtime_register_natives("pymergetic.metal.async.coro", g_async_coro_syms,
                                     (uint32_t)(sizeof(g_async_coro_syms) /
                                                sizeof(g_async_coro_syms[0])))) {
    return -1;
  }
  if (!wasm_runtime_register_natives("pymergetic.metal.mem", g_mem_syms,
                                     (uint32_t)(sizeof(g_mem_syms) / sizeof(g_mem_syms[0])))) {
    return -1;
  }
  if (!wasm_runtime_register_natives("pymergetic.metal.fs", g_fs_syms,
                                     (uint32_t)(sizeof(g_fs_syms) / sizeof(g_fs_syms[0])))) {
    return -1;
  }
  if (pm_metal_wasm_port_register_wasi_stubs() != 0) {
    return -1;
  }
  return 0;
}
