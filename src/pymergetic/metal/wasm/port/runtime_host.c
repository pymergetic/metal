/* Host WAMR port — Alloc_With_Pool from Metal mem (one memory). */
#include "runtime.h"

#include <string.h>

#include <pymergetic/metal/mem/__init__.h>
#include <pymergetic/metal/reg/__init__.h>

#include "wasm_export.h"

#define POOL_BYTES (512u * 1024u)
#define STACK_BYTES (32u * 1024u)
#define HEAP_BYTES (64u * 1024u)
#define MAX_MODS 4
#define MAX_TRAMP 8
#define NAME_MAX 96

typedef struct {
  char name[NAME_MAX];
  wasm_module_t module;
  wasm_module_inst_t inst;
  wasm_exec_env_t exec;
  int used;
} slot_t;

typedef struct {
  int slot;
  char func[64];
  int used;
} tramp_t;

static int g_ready;
static uint8_t *g_pool;
static slot_t g_slots[MAX_MODS];
static tramp_t g_tramps[MAX_TRAMP];

static int cstr_copy(char *dst, size_t dst_n, const uint8_t *src)
{
  size_t i;
  if (src == NULL || dst_n == 0) {
    return -1;
  }
  for (i = 0; i + 1 < dst_n; i++) {
    dst[i] = (char)src[i];
    if (src[i] == 0) {
      return 0;
    }
  }
  dst[dst_n - 1] = 0;
  return -1;
}

static slot_t *find_slot(const char *name)
{
  int i;
  for (i = 0; i < MAX_MODS; i++) {
    if (g_slots[i].used && strcmp(g_slots[i].name, name) == 0) {
      return &g_slots[i];
    }
  }
  return NULL;
}

static slot_t *alloc_slot(void)
{
  int i;
  for (i = 0; i < MAX_MODS; i++) {
    if (!g_slots[i].used) {
      return &g_slots[i];
    }
  }
  return NULL;
}

static void free_slot(slot_t *s)
{
  if (s == NULL || !s->used) {
    return;
  }
  if (s->exec != NULL) {
    wasm_runtime_destroy_exec_env(s->exec);
    s->exec = NULL;
  }
  if (s->inst != NULL) {
    wasm_runtime_deinstantiate(s->inst);
    s->inst = NULL;
  }
  if (s->module != NULL) {
    wasm_runtime_unload(s->module);
    s->module = NULL;
  }
  s->name[0] = 0;
  s->used = 0;
}

static int32_t call_export(slot_t *s, const char *func)
{
  wasm_function_inst_t f;
  uint32_t argv[1];

  if (s == NULL || func == NULL || s->inst == NULL || s->exec == NULL) {
    return -1;
  }
  f = wasm_runtime_lookup_function(s->inst, func);
  if (f == NULL) {
    return -1;
  }
  argv[0] = 0;
  if (!wasm_runtime_call_wasm(s->exec, f, 0, argv)) {
    return -1;
  }
  return (int32_t)argv[0];
}

/* Fixed trampolines for reg_call0 (fn() -> i32). */
static int32_t tramp0(void) { return call_export(&g_slots[g_tramps[0].slot], g_tramps[0].func); }
static int32_t tramp1(void) { return call_export(&g_slots[g_tramps[1].slot], g_tramps[1].func); }
static int32_t tramp2(void) { return call_export(&g_slots[g_tramps[2].slot], g_tramps[2].func); }
static int32_t tramp3(void) { return call_export(&g_slots[g_tramps[3].slot], g_tramps[3].func); }
static int32_t tramp4(void) { return call_export(&g_slots[g_tramps[4].slot], g_tramps[4].func); }
static int32_t tramp5(void) { return call_export(&g_slots[g_tramps[5].slot], g_tramps[5].func); }
static int32_t tramp6(void) { return call_export(&g_slots[g_tramps[6].slot], g_tramps[6].func); }
static int32_t tramp7(void) { return call_export(&g_slots[g_tramps[7].slot], g_tramps[7].func); }

typedef int32_t (*tramp_fn)(void);
static tramp_fn g_tramp_fns[MAX_TRAMP] = {
    tramp0, tramp1, tramp2, tramp3, tramp4, tramp5, tramp6, tramp7,
};

int32_t pm_metal_wasm_port_ready(void)
{
  return g_ready ? 1 : 0;
}

int32_t pm_metal_wasm_port_init(void)
{
  RuntimeInitArgs args;

  if (g_ready) {
    return 0;
  }
  g_pool = pm_metal_mem_alloc(POOL_BYTES);
  if (g_pool == NULL) {
    return -1;
  }
  memset(&args, 0, sizeof(args));
  args.mem_alloc_type = Alloc_With_Pool;
  args.mem_alloc_option.pool.heap_buf = g_pool;
  args.mem_alloc_option.pool.heap_size = (unsigned)POOL_BYTES;
  if (!wasm_runtime_full_init(&args)) {
    pm_metal_mem_free(g_pool);
    g_pool = NULL;
    return -1;
  }
  g_ready = 1;
  return 0;
}

void pm_metal_wasm_port_shutdown(void)
{
  int i;
  if (!g_ready) {
    return;
  }
  for (i = 0; i < MAX_MODS; i++) {
    free_slot(&g_slots[i]);
  }
  for (i = 0; i < MAX_TRAMP; i++) {
    g_tramps[i].used = 0;
  }
  wasm_runtime_destroy();
  if (g_pool != NULL) {
    pm_metal_mem_free(g_pool);
    g_pool = NULL;
  }
  g_ready = 0;
}

int32_t pm_metal_wasm_port_load(const uint8_t *full_module, const uint8_t *bytes, uint32_t len)
{
  char name[NAME_MAX];
  char err[128];
  slot_t *s;
  uint8_t *copy;

  if (!g_ready || full_module == NULL || bytes == NULL || len == 0) {
    return -1;
  }
  if (cstr_copy(name, sizeof(name), full_module) != 0) {
    return -1;
  }
  s = find_slot(name);
  if (s != NULL) {
    free_slot(s);
  } else {
    s = alloc_slot();
    if (s == NULL) {
      return -1;
    }
  }

  /* WAMR may mutate the buffer during load in some configs — keep a Metal copy. */
  copy = pm_metal_mem_alloc(len);
  if (copy == NULL) {
    return -1;
  }
  memcpy(copy, bytes, len);
  memset(err, 0, sizeof(err));
  s->module = wasm_runtime_load(copy, len, err, (uint32_t)sizeof(err));
  pm_metal_mem_free(copy);
  if (s->module == NULL) {
    return -1;
  }
  s->inst = wasm_runtime_instantiate(s->module, STACK_BYTES, HEAP_BYTES, err, (uint32_t)sizeof(err));
  if (s->inst == NULL) {
    wasm_runtime_unload(s->module);
    s->module = NULL;
    return -1;
  }
  s->exec = wasm_runtime_create_exec_env(s->inst, STACK_BYTES);
  if (s->exec == NULL) {
    wasm_runtime_deinstantiate(s->inst);
    wasm_runtime_unload(s->module);
    s->inst = NULL;
    s->module = NULL;
    return -1;
  }
  memcpy(s->name, name, sizeof(s->name));
  s->used = 1;
  return 0;
}

void pm_metal_wasm_port_unload(const uint8_t *full_module)
{
  char name[NAME_MAX];
  slot_t *s;
  if (full_module == NULL || cstr_copy(name, sizeof(name), full_module) != 0) {
    return;
  }
  s = find_slot(name);
  free_slot(s);
}

int32_t pm_metal_wasm_port_call0(const uint8_t *full_module, const uint8_t *func)
{
  char name[NAME_MAX];
  char fname[64];
  slot_t *s;
  if (!g_ready || full_module == NULL || func == NULL) {
    return -1;
  }
  if (cstr_copy(name, sizeof(name), full_module) != 0) {
    return -1;
  }
  if (cstr_copy(fname, sizeof(fname), func) != 0) {
    return -1;
  }
  s = find_slot(name);
  return call_export(s, fname);
}

int32_t pm_metal_wasm_port_publish_reg(const uint8_t *full_module)
{
  char name[NAME_MAX];
  slot_t *s;
  int32_t nexp;
  int32_t i;
  int32_t published;
  int slot_idx;

  if (!g_ready || full_module == NULL) {
    return -1;
  }
  if (cstr_copy(name, sizeof(name), full_module) != 0) {
    return -1;
  }
  s = find_slot(name);
  if (s == NULL || s->module == NULL) {
    return -1;
  }
  slot_idx = (int)(s - g_slots);
  nexp = wasm_runtime_get_export_count(s->module);
  if (nexp < 0) {
    return -1;
  }
  published = 0;
  for (i = 0; i < nexp; i++) {
    wasm_export_t ex;
    int t;
    tramp_fn fn;
    memset(&ex, 0, sizeof(ex));
    wasm_runtime_get_export_type(s->module, i, &ex);
    if (ex.kind != WASM_IMPORT_EXPORT_KIND_FUNC || ex.name == NULL) {
      continue;
    }
    /* Only () -> i32 for reg_call0. */
    if (wasm_func_type_get_param_count(ex.u.func_type) != 0
        || wasm_func_type_get_result_count(ex.u.func_type) != 1) {
      continue;
    }
    if (wasm_func_type_get_result_valkind(ex.u.func_type, 0) != WASM_I32) {
      continue;
    }
    for (t = 0; t < MAX_TRAMP; t++) {
      if (!g_tramps[t].used) {
        break;
      }
    }
    if (t >= MAX_TRAMP) {
      break;
    }
    g_tramps[t].slot = slot_idx;
    if (cstr_copy(g_tramps[t].func, sizeof(g_tramps[t].func), (const uint8_t *)ex.name) != 0) {
      continue;
    }
    g_tramps[t].used = 1;
    fn = g_tramp_fns[t];
    if (pm_metal_reg_register((const uint8_t *)name, (const uint8_t *)ex.name, (const void *)fn)
        != 0) {
      g_tramps[t].used = 0;
      return -1;
    }
    published++;
  }
  return published;
}
