/** @file
  Mem limit catalog — linker-section walk, shell `limits`, WASI natives
  under pymergetic.metal.mem.limit.
**/
#include <pymergetic/metal/runtime/mem/limit.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <runtime/time/time.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wasm_export.h"

#if !defined(__wasm__)

static uint32_t LimTableCount(void)
{
  uintptr_t addr;
  uintptr_t end;
  uint32_t  n;

  addr = (uintptr_t)__pm_metal_mem_limits_start;
  end  = (uintptr_t)__pm_metal_mem_limits_end;
  n    = 0u;
  while (addr < end) {
    n++;
    addr += sizeof(pm_metal_mem_limit_table_t);
  }
  return n;
}

static const pm_metal_mem_limit_table_t *LimTableAt(uint32_t i)
{
  return (const pm_metal_mem_limit_table_t *)((uintptr_t)__pm_metal_mem_limits_start +
                                              (uintptr_t)i * sizeof(pm_metal_mem_limit_table_t));
}

static int32_t LimCopyAt(uint32_t flat_idx, pm_metal_mem_limit_t *out)
{
  uint32_t t_i;
  uint32_t seen;

  if (out == NULL) {
    return -1;
  }

  seen = 0u;
  for (t_i = 0u; t_i < LimTableCount(); t_i++) {
    const pm_metal_mem_limit_table_t *t = LimTableAt(t_i);
    uint32_t                          j;

    if (t->limits == NULL || t->count == 0u) {
      continue;
    }
    for (j = 0u; j < t->count; j++) {
      if (seen == flat_idx) {
        *out = t->limits[j];
        return 0;
      }
      seen++;
    }
  }
  return -1;
}

uint32_t pm_metal_mem_limit_count(void)
{
  uint32_t t_i;
  uint32_t n;

  n = 0u;
  for (t_i = 0u; t_i < LimTableCount(); t_i++) {
    const pm_metal_mem_limit_table_t *t = LimTableAt(t_i);

    if (t->limits == NULL || t->count == 0u) {
      continue;
    }
    n += t->count;
  }
  return n;
}

int32_t pm_metal_mem_limit_get(uint32_t idx, pm_metal_mem_limit_t *out)
{
  return LimCopyAt(idx, out);
}

int32_t pm_metal_mem_limit_find(const char *id, pm_metal_mem_limit_t *out)
{
  uint32_t i;
  uint32_t n;

  if (id == NULL || id[0] == '\0' || out == NULL) {
    return -1;
  }

  n = pm_metal_mem_limit_count();
  for (i = 0u; i < n; i++) {
    pm_metal_mem_limit_t e;

    if (LimCopyAt(i, &e) != 0) {
      return -1;
    }
    if (e.id != NULL && strcmp(e.id, id) == 0) {
      *out = e;
      return 0;
    }
  }
  return -1;
}

int32_t pm_metal_mem_limit_find_mn(const char *module, const char *name, pm_metal_mem_limit_t *out)
{
  uint32_t i;
  uint32_t n;

  if (module == NULL || module[0] == '\0' || name == NULL || name[0] == '\0' || out == NULL) {
    return -1;
  }

  n = pm_metal_mem_limit_count();
  for (i = 0u; i < n; i++) {
    pm_metal_mem_limit_t e;

    if (LimCopyAt(i, &e) != 0) {
      return -1;
    }
    if (e.module != NULL && e.name != NULL && strcmp(e.module, module) == 0 &&
        strcmp(e.name, name) == 0) {
      *out = e;
      return 0;
    }
  }
  return -1;
}

static int32_t LimIdOrModuleMatch(const pm_metal_mem_limit_t *e, const char *key)
{
  size_t mlen;

  if (e == NULL || key == NULL) {
    return 0;
  }
  if (e->id != NULL && strcmp(e->id, key) == 0) {
    return 1;
  }
  if (e->module != NULL && strcmp(e->module, key) == 0) {
    return 1;
  }
  /* Prefix: "net" matches "net.asgi" */
  if (e->module != NULL) {
    mlen = strlen(key);
    if (mlen > 0u && strncmp(e->module, key, mlen) == 0 &&
        (e->module[mlen] == '\0' || e->module[mlen] == '.')) {
      return 1;
    }
  }
  return 0;
}

static void LimPrintOne(const pm_metal_mem_limit_t *e, int detail)
{
  char        line[256];
  const char *unit;
  const char *note;

  if (e == NULL || e->id == NULL) {
    return;
  }

  unit = (e->unit != NULL) ? e->unit : "";
  note = (e->note != NULL) ? e->note : "";

  if (!detail) {
    if (unit[0] != '\0') {
      snprintf(line, sizeof(line), "  - %s  %llu %s", e->id, (unsigned long long)e->value, unit);
    } else {
      snprintf(line, sizeof(line), "  - %s  %llu", e->id, (unsigned long long)e->value);
    }
    pm_metal_shell_out(line);
    return;
  }

  snprintf(line, sizeof(line), "  - %s", e->id);
  pm_metal_shell_out(line);
  snprintf(line,
           sizeof(line),
           "    module=%s name=%s value=%llu",
           e->module != NULL ? e->module : "",
           e->name != NULL ? e->name : "",
           (unsigned long long)e->value);
  pm_metal_shell_out(line);
  if (unit[0] != '\0') {
    snprintf(line, sizeof(line), "    unit=%s", unit);
    pm_metal_shell_out(line);
  }
  if (note[0] != '\0') {
    snprintf(line, sizeof(line), "    %s", note);
    pm_metal_shell_out(line);
  }
}

static int32_t LimCheck(const char *name, int32_t ok, uint32_t *fail)
{
  char line[96];

  snprintf(line, sizeof(line), "  %s %s", ok ? "PASS" : "FAIL", name);
  pm_metal_shell_out(line);
  if (!ok && fail != NULL) {
    (*fail)++;
  }
  return ok ? 0 : -1;
}

/*
 * One-shot smoke: C count/get/find/find_mn (+ known seed ids), then Python
 * list/get/module as a shell job. WASI natives share the same host bodies.
 * HTTP: curl /limits + /api/limits (or doc-iface-smoke).
 */
static void LimitsTest(void)
{
  static const char *const k_ids[] = {
    "net.PM_METAL_IO_WIRE_MAX",
    "net.asgi.ASGI_IO_MAX",
    "net.ip.MEM_SIZE",
    "net.tls.PM_METAL_TLS_WIRE_MAX",
    "py.PM_METAL_PY_BLOB_BYTES",
    "guest.wasm.PM_METAL_WASM_HEAP_SIZE",
    "runtime.mem.PM_METAL_HEAP_SEED_BYTES",
    "dev.net.VNET_TX_BUFS",
  };
  static const char *const k_py = "import pymergetic.metal.mem.limit as L\n"
                                  "rows = L.list()\n"
                                  "assert isinstance(rows, list) and len(rows) > 0\n"
                                  "r0 = rows[0]\n"
                                  "got = L.get(r0['id'])\n"
                                  "assert got is not None and got['id'] == r0['id']\n"
                                  "assert L.get('__no_such_limit__') is None\n"
                                  "mod = L.module(r0['module'])\n"
                                  "assert any(x['id'] == r0['id'] for x in mod)\n"
                                  "assert L.get('net.asgi.ASGI_IO_MAX') is not None\n"
                                  "assert L.module('net')\n"
                                  "print('limits py: PASS n=%d' % len(rows))\n";

  pm_metal_mem_limit_t    e;
  pm_metal_mem_limit_t    e2;
  pm_metal_async_handle_t task_h;
  char                    line[128];
  uint32_t                fail;
  uint32_t                i;
  uint32_t                n;
  uint64_t                deadline;

  fail = 0u;
  n    = pm_metal_mem_limit_count();
  snprintf(line, sizeof(line), "limits test: C API (n=%u)", n);
  pm_metal_shell_out(line);

  (void)LimCheck("count>0", n > 0u, &fail);
  (void)LimCheck("get(0)", n > 0u && pm_metal_mem_limit_get(0u, &e) == 0 && e.id != NULL, &fail);
  if (n > 0u && e.id != NULL) {
    (void)LimCheck(
      "find(id)", pm_metal_mem_limit_find(e.id, &e2) == 0 && e2.value == e.value, &fail);
    (void)LimCheck("find_mn",
                   e.module != NULL && e.name != NULL &&
                     pm_metal_mem_limit_find_mn(e.module, e.name, &e2) == 0 && e2.value == e.value,
                   &fail);
  }
  (void)LimCheck("find(miss)", pm_metal_mem_limit_find("__no_such_limit__", &e2) != 0, &fail);
  (void)LimCheck("get(oob)", pm_metal_mem_limit_get(n, &e2) != 0, &fail);

  for (i = 0u; i < sizeof(k_ids) / sizeof(k_ids[0]); i++) {
    snprintf(line, sizeof(line), "seed %s", k_ids[i]);
    (void)LimCheck(line, pm_metal_mem_limit_find(k_ids[i], &e2) == 0, &fail);
  }

  if (fail != 0u) {
    snprintf(line, sizeof(line), "limits test: C FAIL (%u)", fail);
    pm_metal_shell_out(line);
    return;
  }
  pm_metal_shell_out("limits test: C PASS");

  if (!pm_metal_py_ready()) {
    pm_metal_shell_out("limits test: py SKIP (not ready)");
    return;
  }
  if (pm_metal_shell_job_busy()) {
    pm_metal_shell_out("limits test: py SKIP (shell busy)");
    return;
  }

  task_h = pm_metal_py_run_str(k_py);
  if (task_h == 0) {
    pm_metal_shell_out("limits test: py FAIL (start)");
    return;
  }
  deadline = pm_metal_time_mono_us() + 60000000ull;
  if (pm_metal_shell_job_start("limits-py", task_h, 0, NULL, deadline) != 0) {
    pm_metal_async_task_cancel(task_h);
    pm_metal_shell_out("limits test: py FAIL (job)");
    return;
  }
  pm_metal_shell_out("limits test: py ...");
}

static void LimitsShellCmd(int argc, char **argv)
{
  pm_metal_mem_limit_t e;
  char                 line[96];
  uint32_t             i;
  uint32_t             n;
  uint32_t             shown;

  if (argc >= 2 && argv[1] != NULL && strcmp(argv[1], "test") == 0) {
    LimitsTest();
    return;
  }

  if (argc >= 2) {
    if (pm_metal_mem_limit_find(argv[1], &e) == 0) {
      LimPrintOne(&e, 1);
      return;
    }
    /* Filter by module / prefix */
    n     = pm_metal_mem_limit_count();
    shown = 0u;
    for (i = 0u; i < n; i++) {
      if (pm_metal_mem_limit_get(i, &e) != 0) {
        continue;
      }
      if (LimIdOrModuleMatch(&e, argv[1])) {
        LimPrintOne(&e, 0);
        shown++;
      }
    }
    if (shown == 0u) {
      snprintf(line, sizeof(line), "limits: %s: not found", argv[1]);
      pm_metal_shell_out(line);
    }
    return;
  }

  n = pm_metal_mem_limit_count();
  if (n == 0u) {
    pm_metal_shell_out("limits: (none registered)");
    return;
  }
  for (i = 0u; i < n; i++) {
    if (pm_metal_mem_limit_get(i, &e) == 0) {
      LimPrintOne(&e, 0);
    }
  }
}

PM_METAL_SHELL_CMD(g_pm_metal_shell_cmd_limits,
                   "limits",
                   "limits [id|module|test]  mem/buffer budgets (test=C+py smoke)",
                   LimitsShellCmd);

typedef struct {
  char     id[64];
  char     module[32];
  char     name[48];
  uint64_t value;
  char     unit[16];
  char     note[96];
} pm_metal_mem_limit_info_host_t;

static void LimFillInfo(const pm_metal_mem_limit_t *e, pm_metal_mem_limit_info_host_t *info)
{
  memset(info, 0, sizeof(*info));
  snprintf(info->id, sizeof(info->id), "%s", e->id != NULL ? e->id : "");
  snprintf(info->module, sizeof(info->module), "%s", e->module != NULL ? e->module : "");
  snprintf(info->name, sizeof(info->name), "%s", e->name != NULL ? e->name : "");
  info->value = e->value;
  snprintf(info->unit, sizeof(info->unit), "%s", e->unit != NULL ? e->unit : "");
  snprintf(info->note, sizeof(info->note), "%s", e->note != NULL ? e->note : "");
}

static uint32_t pm_metal_mem_limit_count_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_mem_limit_count();
}

static int32_t pm_metal_mem_limit_get_native(wasm_exec_env_t exec_env, uint32_t idx, uint32_t out)
{
  wasm_module_inst_t             inst;
  void                          *native;
  pm_metal_mem_limit_t           e;
  pm_metal_mem_limit_info_host_t info;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || !wasm_runtime_validate_app_addr(inst, out, sizeof(info))) {
    return -1;
  }
  if (pm_metal_mem_limit_get(idx, &e) != 0) {
    return -1;
  }
  LimFillInfo(&e, &info);
  native = wasm_runtime_addr_app_to_native(inst, out);
  if (native == NULL) {
    return -1;
  }
  memcpy(native, &info, sizeof(info));
  return 0;
}

static int32_t pm_metal_mem_limit_find_native(wasm_exec_env_t exec_env,
                                              const char     *id,
                                              uint32_t        out)
{
  wasm_module_inst_t             inst;
  void                          *native;
  pm_metal_mem_limit_t           e;
  pm_metal_mem_limit_info_host_t info;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || !wasm_runtime_validate_app_addr(inst, out, sizeof(info))) {
    return -1;
  }
  if (pm_metal_mem_limit_find(id, &e) != 0) {
    return -1;
  }
  LimFillInfo(&e, &info);
  native = wasm_runtime_addr_app_to_native(inst, out);
  if (native == NULL) {
    return -1;
  }
  memcpy(native, &info, sizeof(info));
  return 0;
}

static int32_t pm_metal_mem_limit_find_mn_native(wasm_exec_env_t exec_env,
                                                 const char     *module,
                                                 const char     *name,
                                                 uint32_t        out)
{
  wasm_module_inst_t             inst;
  void                          *native;
  pm_metal_mem_limit_t           e;
  pm_metal_mem_limit_info_host_t info;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || !wasm_runtime_validate_app_addr(inst, out, sizeof(info))) {
    return -1;
  }
  if (pm_metal_mem_limit_find_mn(module, name, &e) != 0) {
    return -1;
  }
  LimFillInfo(&e, &info);
  native = wasm_runtime_addr_app_to_native(inst, out);
  if (native == NULL) {
    return -1;
  }
  memcpy(native, &info, sizeof(info));
  return 0;
}

static NativeSymbol g_pm_metal_mem_limit_native_symbols[] = {
  { "pm_metal_mem_limit_count", (void *)pm_metal_mem_limit_count_native, "()I", NULL },
  { "pm_metal_mem_limit_get", (void *)pm_metal_mem_limit_get_native, "(ii)i", NULL },
  { "pm_metal_mem_limit_find", (void *)pm_metal_mem_limit_find_native, "($i)i", NULL },
  { "pm_metal_mem_limit_find_mn", (void *)pm_metal_mem_limit_find_mn_native, "($$i)i", NULL },
};

int pm_metal_mem_limit_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_MEM_LIMIT_WASI_MODULE,
                                     g_pm_metal_mem_limit_native_symbols,
                                     sizeof(g_pm_metal_mem_limit_native_symbols) /
                                       sizeof(g_pm_metal_mem_limit_native_symbols[0]))) {
    return -1;
  }
  return 0;
}

#endif /* !__wasm__ */
