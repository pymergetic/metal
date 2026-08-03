/*
 * Guest coro: host step trampoline calls a wasm export with self_h;
 * durable frame lives in Metal heap and is pinned into linear memory
 * for the duration of each step (product coro_frame shape).
 */
#include "guest_coro.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/async/coro.h>
#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/runner.h>
#include <pymergetic/metal/async/task.h>
#include <pymergetic/metal/log/__init__.h>

#ifndef PM_METAL_GUEST_CORO_MAX
/* Keep in sync with async MAX_HANDLES — guest slots are indexed by handle. */
#define PM_METAL_GUEST_CORO_MAX 512u
#endif

/* Deep C stack inside the step trampoline for heavy guest steps. */
#ifndef PM_METAL_GUEST_CORO_STACK
#define PM_METAL_GUEST_CORO_STACK (1024u * 1024u)
#endif

typedef struct {
  int32_t used;
  int32_t own_exec; /* 1 if exec was created for this coro */
  wasm_module_inst_t inst;
  wasm_exec_env_t exec;
  wasm_function_inst_t step_fn;
  uint32_t guest_off;
  uint32_t state_bytes;
} guest_slot_t;

static guest_slot_t g_slots[PM_METAL_GUEST_CORO_MAX + 1u];
static guest_slot_t *g_running;
static char g_step_name[64] = "step";

static guest_slot_t *slot_get(uint32_t h)
{
  if (h == 0u || h > PM_METAL_GUEST_CORO_MAX) {
    return NULL;
  }
  if (g_slots[h].used == 0) {
    return NULL;
  }
  return &g_slots[h];
}

static void slot_clear(uint32_t h)
{
  guest_slot_t *s;

  if (h == 0u || h > PM_METAL_GUEST_CORO_MAX) {
    return;
  }
  s = &g_slots[h];
  if (s->guest_off != 0u && s->inst != NULL) {
    wasm_runtime_module_free(s->inst, (uint64_t)s->guest_off);
  }
  if (s->own_exec != 0 && s->exec != NULL) {
    wasm_runtime_destroy_exec_env(s->exec);
  }
  memset(s, 0, sizeof(*s));
}

void pm_metal_wasm_guest_callin_set_step(const char *name)
{
  size_t n;
  size_t i;

  if (name == NULL || name[0] == '\0') {
    g_step_name[0] = 's';
    g_step_name[1] = 't';
    g_step_name[2] = 'e';
    g_step_name[3] = 'p';
    g_step_name[4] = '\0';
    return;
  }
  n = 0u;
  while (name[n] != '\0' && n + 1u < sizeof(g_step_name)) {
    n++;
  }
  for (i = 0u; i < n; i++) {
    g_step_name[i] = name[i];
  }
  g_step_name[n] = '\0';
}

static int32_t pin(guest_slot_t *s, uint32_t h)
{
  uint8_t *host;
  void *native;
  uint64_t off;

  if (s == NULL) {
    return -1;
  }
  if (s->guest_off != 0u) {
    return 0;
  }
  host = pm_metal_async_coro_state(h);
  if (host == NULL || s->state_bytes == 0u) {
    return 0;
  }
  native = NULL;
  off = wasm_runtime_module_malloc(s->inst, s->state_bytes, &native);
  if (off == 0u || native == NULL) {
    return -1;
  }
  memcpy(native, host, (size_t)s->state_bytes);
  s->guest_off = (uint32_t)off;
  return 0;
}

static void unpin(guest_slot_t *s, uint32_t h)
{
  uint8_t *host;
  void *native;

  if (s == NULL || s->guest_off == 0u || s->inst == NULL) {
    return;
  }
  host = pm_metal_async_coro_state(h);
  if (host != NULL && s->state_bytes > 0u) {
    native = wasm_runtime_addr_app_to_native(s->inst, (uint64_t)s->guest_off);
    if (native != NULL) {
      memcpy(host, native, (size_t)s->state_bytes);
    }
  }
  wasm_runtime_module_free(s->inst, (uint64_t)s->guest_off);
  s->guest_off = 0u;
}

static uint32_t guest_step(uint32_t h)
{
  guest_slot_t *s;
  guest_slot_t *prev;
  uint32_t argv[1];

  s = slot_get(h);
  if (s == NULL || s->exec == NULL || s->step_fn == NULL || s->inst == NULL) {
    return (uint32_t)PM_METAL_ASYNC_ERROR;
  }
  if (pin(s, h) != 0) {
    return (uint32_t)PM_METAL_ASYNC_ERROR;
  }
  prev = g_running;
  g_running = s;
  argv[0] = h;
  if (!wasm_runtime_call_wasm(s->exec, s->step_fn, 1, argv)) {
    {
      const char *ex;

      ex = wasm_runtime_get_exception(s->inst);
      if (ex != NULL && ex[0] != '\0') {
        pm_metal_log((const uint8_t *)ex);
      } else {
        pm_metal_log((const uint8_t *)"guest coro: wasm exception\0");
      }
    }
    wasm_runtime_clear_exception(s->inst);
    g_running = prev;
    unpin(s, h);
    return (uint32_t)PM_METAL_ASYNC_ERROR;
  }
  g_running = prev;
  unpin(s, h);
  return argv[0];
}

uint32_t pm_metal_wasm_guest_coro_create_inst(wasm_module_inst_t inst, uint32_t state_bytes)
{
  wasm_exec_env_t exec;
  wasm_function_inst_t step_fn;
  uint32_t h;
  guest_slot_t *s;

  if (inst == NULL) {
    return 0u;
  }
  step_fn = wasm_runtime_lookup_function(inst, g_step_name);
  if (step_fn == NULL) {
    return 0u;
  }
  /* Dedicated exec_env so step call-ins are not nested on the caller's
   * WAMR stack (ready -> smoke -> poll -> step). */
  exec = wasm_runtime_create_exec_env(inst, PM_METAL_GUEST_CORO_STACK);
  if (exec == NULL) {
    return 0u;
  }

  h = pm_metal_async_coro_create(guest_step, state_bytes);
  if (h == 0u || h > PM_METAL_GUEST_CORO_MAX) {
    wasm_runtime_destroy_exec_env(exec);
    if (h != 0u) {
      pm_metal_async_coro_close(h);
    }
    return 0u;
  }
  s = &g_slots[h];
  memset(s, 0, sizeof(*s));
  s->used = 1;
  s->own_exec = 1;
  s->inst = inst;
  s->exec = exec;
  s->step_fn = step_fn;
  s->state_bytes = state_bytes;
  return h;
}

uint32_t pm_metal_wasm_guest_coro_create(wasm_exec_env_t exec_env, uint32_t state_bytes)
{
  wasm_module_inst_t inst;
  wasm_function_inst_t step_fn;

  inst = NULL;
  step_fn = NULL;
  if (g_running != NULL) {
    /* Nested create: same module/step. */
    inst = g_running->inst;
    step_fn = g_running->step_fn;
    if (inst == NULL || step_fn == NULL) {
      return 0u;
    }
    /* Temporarily force step_fn via g_step_name path: create_inst looks up
     * g_step_name; nested create must keep the parent's step. Stash by
     * calling create_inst only when we already resolved step_fn — so
     * duplicate lookup is fine when names match. */
    return pm_metal_wasm_guest_coro_create_inst(inst, state_bytes);
  }
  if (exec_env != NULL) {
    inst = wasm_runtime_get_module_inst(exec_env);
  }
  return pm_metal_wasm_guest_coro_create_inst(inst, state_bytes);
}

uint32_t pm_metal_wasm_guest_coro_state(uint32_t h)
{
  guest_slot_t *s;

  s = slot_get(h);
  if (s == NULL) {
    return 0u;
  }
  return s->guest_off;
}

uint32_t pm_metal_wasm_guest_coro_alloc(uint32_t h, uint32_t n)
{
  guest_slot_t *s;
  uint8_t *host;

  s = slot_get(h);
  if (s == NULL || n == 0u) {
    return 0u;
  }
  host = pm_metal_async_coro_state(h);
  if (host != NULL) {
    if (s->guest_off != 0u) {
      return s->guest_off;
    }
    if (g_running == s && pin(s, h) == 0) {
      return s->guest_off;
    }
    return 0u;
  }
  host = pm_metal_async_coro_alloc(h, n);
  if (host == NULL) {
    return 0u;
  }
  s->state_bytes = n;
  if (g_running == s) {
    if (pin(s, h) != 0) {
      return 0u;
    }
    return s->guest_off;
  }
  return 0u;
}

void pm_metal_wasm_guest_coro_close(uint32_t h)
{
  slot_clear(h);
  pm_metal_async_coro_close(h);
}

int32_t pm_metal_wasm_guest_coro_smoke(wasm_exec_env_t exec_env)
{
  uint32_t h;
  uint32_t th;
  int32_t i;
  pm_metal_async_status_t st;

  h = pm_metal_wasm_guest_coro_create(exec_env, 16u);
  if (h == 0u) {
    return -1;
  }
  th = pm_metal_async_create_task(h);
  if (th == 0u) {
    pm_metal_wasm_guest_coro_close(h);
    return -1;
  }
  for (i = 0; i < 64; i++) {
    (void)pm_metal_async_run_poll_all();
    st = pm_metal_async_status(h);
    if (st == PM_METAL_ASYNC_DONE) {
      return 0;
    }
    if (st == PM_METAL_ASYNC_ERROR || st == PM_METAL_ASYNC_CANCELLED) {
      return -1;
    }
  }
  return -1;
}
