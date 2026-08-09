/*
 * Host (kernel) natives for guest_surface modules.
 * Import module strings must match forge faces (stem path), e.g.
 * pymergetic.metal.async.time — not the package root alone.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/async/await.h>
#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/process.h>
#include <pymergetic/metal/async/task.h>
#include <pymergetic/metal/async/time.h>

#include "guest_coro.h"
#include <pymergetic/metal/dev/audio/__init__.h>
#include <pymergetic/metal/dev/gfx/__init__.h>
#include <pymergetic/metal/dev/input/__init__.h>
#include <pymergetic/metal/fs/__init__.h>
#include <pymergetic/metal/log/__init__.h>
#include <pymergetic/metal/mem/__init__.h>
#include <pymergetic/metal/shell/__init__.h>

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

/* ---- async.coro (guest: create inherits call-in step; state is linear) ---- */

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

/* ---- async.process ---- */

static uint32_t native_process_crown(wasm_exec_env_t exec_env, uint32_t task_h)
{
  (void)exec_env;
  return pm_metal_async_process_crown(task_h);
}

static uint32_t native_process_handle(wasm_exec_env_t exec_env, uint32_t pid)
{
  (void)exec_env;
  return pm_metal_async_process_handle(pid);
}

static int32_t native_process_kill(wasm_exec_env_t exec_env, uint32_t pid)
{
  (void)exec_env;
  return pm_metal_async_process_kill(pid);
}

static NativeSymbol g_async_process_syms[] = {
    {"pm_metal_async_process_crown", (void *)native_process_crown, "(i)i", NULL},
    {"pm_metal_async_process_handle", (void *)native_process_handle, "(i)i", NULL},
    {"pm_metal_async_process_kill", (void *)native_process_kill, "(i)i", NULL},
};

/* ---- mem cookies (guest u32 handles; host face still uses pointers) ---- */

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

static int32_t native_mem_copy_out(wasm_exec_env_t exec_env,
                                   uint32_t src_cookie,
                                   uint32_t dest_off,
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

static int32_t native_mem_copy_in(wasm_exec_env_t exec_env,
                                  uint32_t dest_cookie,
                                  uint32_t src_off,
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

static int32_t native_mem_copy_out_at(wasm_exec_env_t exec_env,
                                      uint32_t src_cookie,
                                      uint32_t src_off,
                                      uint32_t dest_off,
                                      uint32_t n)
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

/* ---- fs (paths as $, cookies as i) ---- */

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

static uint32_t native_fs_read_mem_async(wasm_exec_env_t exec_env,
                                         char *path,
                                         uint32_t dest_cookie,
                                         uint32_t dest_len)
{
  (void)exec_env;
  if (path == NULL) {
    return 0u;
  }
  return pm_metal_fs_read_mem_async((const uint8_t *)path, dest_cookie, dest_len);
}

static uint32_t native_fs_write_mem_async(wasm_exec_env_t exec_env,
                                          char *path,
                                          uint32_t src_cookie,
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

/* ---- gfx ---- */

static int32_t native_gfx_ready(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_dev_gfx_ready();
}

static int32_t native_gfx_width(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_dev_gfx_width();
}

static int32_t native_gfx_height(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_dev_gfx_height();
}

static int32_t native_gfx_present(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_dev_gfx_present();
}

static uint32_t native_gfx_present_async(wasm_exec_env_t exec_env, uint32_t surface)
{
  (void)exec_env;
  return pm_metal_dev_gfx_present_async(surface);
}

static int32_t native_gfx_blit_bgra(wasm_exec_env_t exec_env,
                                    int32_t dx,
                                    int32_t dy,
                                    int32_t dw,
                                    int32_t dh,
                                    uint32_t app_ptr,
                                    int32_t src_w,
                                    int32_t src_h,
                                    int32_t src_pitch)
{
  wasm_module_inst_t inst;
  void *native;
  uint64_t nbytes;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || app_ptr == 0u) {
    return -1;
  }
  if (src_w <= 0 || src_h <= 0 || src_pitch < src_w * 4) {
    return -1;
  }
  nbytes = (uint64_t)src_pitch * (uint64_t)src_h;
  if (!wasm_runtime_validate_app_addr(inst, (uint64_t)app_ptr, nbytes)) {
    return -1;
  }
  native = wasm_runtime_addr_app_to_native(inst, (uint64_t)app_ptr);
  if (native == NULL) {
    return -1;
  }
  return pm_metal_dev_gfx_blit_bgra(dx, dy, dw, dh, (const uint8_t *)native, src_w, src_h,
                                    src_pitch);
}

static NativeSymbol g_gfx_syms[] = {
    {"pm_metal_dev_gfx_ready", (void *)native_gfx_ready, "()i", NULL},
    {"pm_metal_dev_gfx_width", (void *)native_gfx_width, "()i", NULL},
    {"pm_metal_dev_gfx_height", (void *)native_gfx_height, "()i", NULL},
    {"pm_metal_dev_gfx_present", (void *)native_gfx_present, "()i", NULL},
    {"pm_metal_dev_gfx_present_async", (void *)native_gfx_present_async, "(i)i", NULL},
    {"pm_metal_dev_gfx_blit_bgra", (void *)native_gfx_blit_bgra, "(iiiiiiii)i", NULL},
};

/* ---- shell ---- */

static void native_shell_log(wasm_exec_env_t exec_env, char *line)
{
  (void)exec_env;
  if (line == NULL) {
    return;
  }
  pm_metal_shell_log((const uint8_t *)line);
}

static NativeSymbol g_shell_syms[] = {
    {"pm_metal_shell_log", (void *)native_shell_log, "($)", NULL},
};

/* ---- input ---- */

static void native_input_poll(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  pm_metal_dev_input_poll();
}

static int32_t native_input_poll_key_event(wasm_exec_env_t exec_env, uint32_t dest_off)
{
  wasm_module_inst_t inst;
  void *native;
  pm_metal_dev_input_key_event_t ev;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || dest_off == 0u) {
    return 0;
  }
  if (!wasm_runtime_validate_app_addr(inst, (uint64_t)dest_off, sizeof(ev))) {
    return 0;
  }
  native = wasm_runtime_addr_app_to_native(inst, (uint64_t)dest_off);
  if (native == NULL) {
    return 0;
  }
  if (pm_metal_dev_input_poll_key_event(&ev) == 0) {
    return 0;
  }
  memcpy(native, &ev, sizeof(ev));
  return 1;
}

static int32_t native_input_poll_pointer(wasm_exec_env_t exec_env, uint32_t dest_off)
{
  wasm_module_inst_t inst;
  void *native;
  pm_metal_dev_input_pointer_t ev;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || dest_off == 0u) {
    return 0;
  }
  if (!wasm_runtime_validate_app_addr(inst, (uint64_t)dest_off, sizeof(ev))) {
    return 0;
  }
  native = wasm_runtime_addr_app_to_native(inst, (uint64_t)dest_off);
  if (native == NULL) {
    return 0;
  }
  if (pm_metal_dev_input_poll_pointer(&ev) == 0) {
    return 0;
  }
  memcpy(native, &ev, sizeof(ev));
  return 1;
}

static int32_t native_input_pointer_lock(wasm_exec_env_t exec_env, uint32_t surface)
{
  (void)exec_env;
  return pm_metal_dev_input_pointer_lock(surface);
}

static void native_input_pointer_unlock(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  pm_metal_dev_input_pointer_unlock();
}

static int32_t native_input_pointer_locked(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_dev_input_pointer_locked();
}

static void native_input_push_key(wasm_exec_env_t exec_env, int32_t pressed, uint32_t code)
{
  (void)exec_env;
  pm_metal_dev_input_push_key(pressed, (uint16_t)code);
}

static NativeSymbol g_input_syms[] = {
    {"pm_metal_dev_input_poll", (void *)native_input_poll, "()", NULL},
    {"pm_metal_dev_input_poll_key_event", (void *)native_input_poll_key_event, "(i)i", NULL},
    {"pm_metal_dev_input_poll_pointer", (void *)native_input_poll_pointer, "(i)i", NULL},
    {"pm_metal_dev_input_pointer_lock", (void *)native_input_pointer_lock, "(i)i", NULL},
    {"pm_metal_dev_input_pointer_unlock", (void *)native_input_pointer_unlock, "()", NULL},
    {"pm_metal_dev_input_pointer_locked", (void *)native_input_pointer_locked, "()i", NULL},
    {"pm_metal_dev_input_push_key", (void *)native_input_push_key, "(ii)", NULL},
};

/* ---- audio (null) ---- */

static int32_t native_audio_ready(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_dev_audio_ready();
}

static uint32_t native_audio_open(wasm_exec_env_t exec_env, uint32_t format, uint32_t frames)
{
  (void)exec_env;
  return pm_metal_dev_audio_open(format, frames);
}

static void native_audio_close(wasm_exec_env_t exec_env, uint32_t s)
{
  (void)exec_env;
  pm_metal_dev_audio_close(s);
}

static uint32_t native_audio_queue(wasm_exec_env_t exec_env,
                                  uint32_t s,
                                  uint32_t app_ptr,
                                  uint32_t nbytes)
{
  wasm_module_inst_t inst;
  void *native;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL) {
    return 0;
  }
  if (nbytes == 0u) {
    return pm_metal_dev_audio_queue(s, NULL, 0);
  }
  if (app_ptr == 0u || !wasm_runtime_validate_app_addr(inst, (uint64_t)app_ptr, (uint64_t)nbytes)) {
    return 0;
  }
  native = wasm_runtime_addr_app_to_native(inst, (uint64_t)app_ptr);
  if (native == NULL) {
    return 0;
  }
  return pm_metal_dev_audio_queue(s, (const uint8_t *)native, nbytes);
}

static uint32_t native_audio_drain(wasm_exec_env_t exec_env, uint32_t s, uint32_t nbytes)
{
  (void)exec_env;
  return pm_metal_dev_audio_drain(s, nbytes);
}

static void native_audio_mute(wasm_exec_env_t exec_env, int32_t on)
{
  (void)exec_env;
  pm_metal_dev_audio_mute(on);
}

static int32_t native_audio_muted(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_dev_audio_muted();
}

static void native_audio_volume_set(wasm_exec_env_t exec_env, uint32_t pct)
{
  (void)exec_env;
  pm_metal_dev_audio_volume_set(pct);
}

static uint32_t native_audio_volume_get(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_dev_audio_volume_get();
}

static int32_t native_audio_backend(wasm_exec_env_t exec_env, uint32_t app_ptr, uint32_t out_cap)
{
  wasm_module_inst_t inst;
  void *native;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || app_ptr == 0u || out_cap == 0u) {
    return -1;
  }
  if (!wasm_runtime_validate_app_addr(inst, (uint64_t)app_ptr, (uint64_t)out_cap)) {
    return -1;
  }
  native = wasm_runtime_addr_app_to_native(inst, (uint64_t)app_ptr);
  if (native == NULL) {
    return -1;
  }
  return pm_metal_dev_audio_backend((uint8_t *)native, out_cap);
}

static NativeSymbol g_audio_syms[] = {
    {"pm_metal_dev_audio_ready", (void *)native_audio_ready, "()i", NULL},
    {"pm_metal_dev_audio_open", (void *)native_audio_open, "(ii)i", NULL},
    {"pm_metal_dev_audio_close", (void *)native_audio_close, "(i)", NULL},
    {"pm_metal_dev_audio_queue", (void *)native_audio_queue, "(iii)i", NULL},
    {"pm_metal_dev_audio_drain", (void *)native_audio_drain, "(ii)i", NULL},
    {"pm_metal_dev_audio_mute", (void *)native_audio_mute, "(i)", NULL},
    {"pm_metal_dev_audio_muted", (void *)native_audio_muted, "()i", NULL},
    {"pm_metal_dev_audio_volume_set", (void *)native_audio_volume_set, "(i)", NULL},
    {"pm_metal_dev_audio_volume_get", (void *)native_audio_volume_get, "()i", NULL},
    {"pm_metal_dev_audio_backend", (void *)native_audio_backend, "(ii)i", NULL},
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
  if (!wasm_runtime_register_natives("pymergetic.metal.async.process", g_async_process_syms,
                                     (uint32_t)(sizeof(g_async_process_syms) /
                                                sizeof(g_async_process_syms[0])))) {
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
  if (!wasm_runtime_register_natives("pymergetic.metal.dev.gfx", g_gfx_syms,
                                     (uint32_t)(sizeof(g_gfx_syms) / sizeof(g_gfx_syms[0])))) {
    return -1;
  }
  if (!wasm_runtime_register_natives("pymergetic.metal.shell", g_shell_syms,
                                     (uint32_t)(sizeof(g_shell_syms) / sizeof(g_shell_syms[0])))) {
    return -1;
  }
  if (!wasm_runtime_register_natives("pymergetic.metal.dev.input", g_input_syms,
                                     (uint32_t)(sizeof(g_input_syms) /
                                                sizeof(g_input_syms[0])))) {
    return -1;
  }
  if (!wasm_runtime_register_natives("pymergetic.metal.dev.audio", g_audio_syms,
                                     (uint32_t)(sizeof(g_audio_syms) /
                                                sizeof(g_audio_syms[0])))) {
    return -1;
  }
  if (pm_metal_wasm_port_register_wasi_stubs() != 0) {
    return -1;
  }
  return 0;
}
