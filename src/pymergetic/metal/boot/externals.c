/** @file
  Third-party externals registry — linker-section walk, shell `externals`,
  and WASI natives under pymergetic.metal.externals.
**/
#include <pymergetic/metal/boot/externals.h>
#include <pymergetic/metal/shell/shell_cmd.h>

#include <stdio.h>
#include <string.h>

#include "wasm_export.h"

#if !defined(__wasm__)

/* Guest-registered rows (Python autoload / register) — host-owned copies. */
#define PM_METAL_EXTERNAL_DYN_MAX 64u

typedef struct {
  char id[32];
  char version[48];
  char url[96];
  char note[96];
} pm_metal_external_dyn_t;

static pm_metal_external_dyn_t g_ext_dyn[PM_METAL_EXTERNAL_DYN_MAX];
static uint32_t                g_ext_dyn_n;

static uint32_t ExtTableCount(void)
{
  uintptr_t addr;
  uintptr_t end;
  uint32_t  n;

  addr = (uintptr_t)__pm_metal_externals_start;
  end  = (uintptr_t)__pm_metal_externals_end;
  n    = 0u;
  while (addr < end) {
    n++;
    addr += sizeof(pm_metal_external_table_t);
  }
  return n;
}

static const pm_metal_external_table_t *ExtTableAt(uint32_t i)
{
  return (const pm_metal_external_table_t *)((uintptr_t)__pm_metal_externals_start +
                                             (uintptr_t)i * sizeof(pm_metal_external_table_t));
}

static uint32_t ExtStaticFlatCount(void)
{
  uint32_t t_i;
  uint32_t n;

  n = 0u;
  for (t_i = 0u; t_i < ExtTableCount(); t_i++) {
    const pm_metal_external_table_t *t = ExtTableAt(t_i);

    if (t->exts == NULL || t->count == 0u) {
      continue;
    }
    n += t->count;
  }
  return n;
}

static int32_t ExtCopyStaticAt(uint32_t flat_idx, pm_metal_external_t *out)
{
  uint32_t t_i;
  uint32_t seen;

  if (out == NULL) {
    return -1;
  }

  seen = 0u;
  for (t_i = 0u; t_i < ExtTableCount(); t_i++) {
    const pm_metal_external_table_t *t = ExtTableAt(t_i);
    uint32_t                         j;

    if (t->exts == NULL || t->count == 0u) {
      continue;
    }
    for (j = 0u; j < t->count; j++) {
      if (seen == flat_idx) {
        *out = t->exts[j];
        return 0;
      }
      seen++;
    }
  }
  return -1;
}

static int32_t ExtCopyAt(uint32_t flat_idx, pm_metal_external_t *out)
{
  uint32_t static_n;

  if (out == NULL) {
    return -1;
  }

  static_n = ExtStaticFlatCount();
  if (flat_idx < static_n) {
    return ExtCopyStaticAt(flat_idx, out);
  }
  flat_idx -= static_n;
  if (flat_idx >= g_ext_dyn_n) {
    return -1;
  }
  out->id      = g_ext_dyn[flat_idx].id;
  out->version = g_ext_dyn[flat_idx].version;
  out->url     = g_ext_dyn[flat_idx].url;
  out->note    = g_ext_dyn[flat_idx].note;
  return 0;
}

uint32_t pm_metal_external_count(void)
{
  return ExtStaticFlatCount() + g_ext_dyn_n;
}

int32_t pm_metal_external_get(uint32_t idx, pm_metal_external_t *out)
{
  return ExtCopyAt(idx, out);
}

int32_t pm_metal_external_find(const char *id, pm_metal_external_t *out)
{
  uint32_t i;
  uint32_t n;

  if (id == NULL || id[0] == '\0' || out == NULL) {
    return -1;
  }

  /* Dyn last-wins for the same id: scan dyn first, then static. */
  for (i = 0u; i < g_ext_dyn_n; i++) {
    if (strcmp(g_ext_dyn[i].id, id) == 0) {
      out->id      = g_ext_dyn[i].id;
      out->version = g_ext_dyn[i].version;
      out->url     = g_ext_dyn[i].url;
      out->note    = g_ext_dyn[i].note;
      return 0;
    }
  }

  n = ExtStaticFlatCount();
  for (i = 0u; i < n; i++) {
    pm_metal_external_t e;

    if (ExtCopyStaticAt(i, &e) != 0) {
      return -1;
    }
    if (e.id != NULL && strcmp(e.id, id) == 0) {
      *out = e;
      return 0;
    }
  }
  return -1;
}

int32_t pm_metal_external_register(const char *id,
                                   const char *version,
                                   const char *url,
                                   const char *note)
{
  uint32_t i;
  pm_metal_external_dyn_t *slot;

  if (id == NULL || id[0] == '\0') {
    return -1;
  }
  if (version == NULL) {
    version = "";
  }
  if (url == NULL) {
    url = "";
  }
  if (note == NULL) {
    note = "";
  }

  slot = NULL;
  for (i = 0u; i < g_ext_dyn_n; i++) {
    if (strcmp(g_ext_dyn[i].id, id) == 0) {
      slot = &g_ext_dyn[i];
      break;
    }
  }
  if (slot == NULL) {
    if (g_ext_dyn_n >= PM_METAL_EXTERNAL_DYN_MAX) {
      return -1;
    }
    slot = &g_ext_dyn[g_ext_dyn_n];
    g_ext_dyn_n++;
  }

  memset(slot, 0, sizeof(*slot));
  snprintf(slot->id, sizeof(slot->id), "%s", id);
  snprintf(slot->version, sizeof(slot->version), "%s", version);
  snprintf(slot->url, sizeof(slot->url), "%s", url);
  snprintf(slot->note, sizeof(slot->note), "%s", note);
  return 0;
}

static void ExtPrintOne(const pm_metal_external_t *e, int detail)
{
  char        line[192];
  const char *ver;
  const char *url;

  if (e == NULL || e->id == NULL) {
    return;
  }

  ver = (e->version != NULL) ? e->version : "";
  url = (e->url != NULL) ? e->url : "";

  if (!detail) {
    if (ver[0] != '\0' && url[0] != '\0') {
      snprintf(line, sizeof(line), "  - %s %s  %s", e->id, ver, url);
    } else if (ver[0] != '\0') {
      snprintf(line, sizeof(line), "  - %s %s", e->id, ver);
    } else if (url[0] != '\0') {
      snprintf(line, sizeof(line), "  - %s  %s", e->id, url);
    } else {
      snprintf(line, sizeof(line), "  - %s", e->id);
    }
    pm_metal_shell_out(line);
    return;
  }

  if (ver[0] != '\0') {
    snprintf(line, sizeof(line), "  - %s %s", e->id, ver);
  } else {
    snprintf(line, sizeof(line), "  - %s", e->id);
  }
  pm_metal_shell_out(line);
  if (e->note != NULL && e->note[0] != '\0') {
    snprintf(line, sizeof(line), "    %s", e->note);
    pm_metal_shell_out(line);
  }
  if (url[0] != '\0') {
    snprintf(line, sizeof(line), "    %s", url);
    pm_metal_shell_out(line);
  }
}

static void ExternalsShellCmd(int argc, char **argv)
{
  pm_metal_external_t e;
  char                line[96];
  uint32_t            i;
  uint32_t            n;

  if (argc >= 2) {
    if (pm_metal_external_find(argv[1], &e) != 0) {
      snprintf(line, sizeof(line), "externals: %s: not found", argv[1]);
      pm_metal_shell_out(line);
      return;
    }
    ExtPrintOne(&e, 1);
    return;
  }

  n = pm_metal_external_count();
  if (n == 0u) {
    pm_metal_shell_out("externals: (none registered)");
    return;
  }
  for (i = 0u; i < n; i++) {
    if (pm_metal_external_get(i, &e) == 0) {
      ExtPrintOne(&e, 0);
    }
  }
}

PM_METAL_SHELL_CMD(g_pm_metal_shell_cmd_externals,
                   "externals",
                   "externals [id]    third-party stack ids + versions",
                   ExternalsShellCmd);

/* Guest out-buffer layout — must match pm_metal_external_info_t in the header. */
typedef struct {
  char id[32];
  char version[48];
  char url[96];
  char note[96];
} pm_metal_external_info_host_t;

static void ExtFillInfo(const pm_metal_external_t *e, pm_metal_external_info_host_t *info)
{
  const char *id;
  const char *version;
  const char *url;
  const char *note;

  id      = (e->id != NULL) ? e->id : "";
  version = (e->version != NULL) ? e->version : "";
  url     = (e->url != NULL) ? e->url : "";
  note    = (e->note != NULL) ? e->note : "";

  memset(info, 0, sizeof(*info));
  snprintf(info->id, sizeof(info->id), "%s", id);
  snprintf(info->version, sizeof(info->version), "%s", version);
  snprintf(info->url, sizeof(info->url), "%s", url);
  snprintf(info->note, sizeof(info->note), "%s", note);
}

static uint32_t pm_metal_external_count_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_external_count();
}

static int32_t pm_metal_external_get_native(wasm_exec_env_t exec_env, uint32_t idx, uint32_t out)
{
  wasm_module_inst_t             inst;
  void                          *native;
  pm_metal_external_t            e;
  pm_metal_external_info_host_t  info;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || !wasm_runtime_validate_app_addr(inst, out, sizeof(info))) {
    return -1;
  }
  if (pm_metal_external_get(idx, &e) != 0) {
    return -1;
  }
  ExtFillInfo(&e, &info);
  native = wasm_runtime_addr_app_to_native(inst, out);
  if (native == NULL) {
    return -1;
  }
  memcpy(native, &info, sizeof(info));
  return 0;
}

static int32_t pm_metal_external_find_native(wasm_exec_env_t exec_env, const char *id, uint32_t out)
{
  wasm_module_inst_t             inst;
  void                          *native;
  pm_metal_external_t            e;
  pm_metal_external_info_host_t  info;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || !wasm_runtime_validate_app_addr(inst, out, sizeof(info))) {
    return -1;
  }
  if (pm_metal_external_find(id, &e) != 0) {
    return -1;
  }
  ExtFillInfo(&e, &info);
  native = wasm_runtime_addr_app_to_native(inst, out);
  if (native == NULL) {
    return -1;
  }
  memcpy(native, &info, sizeof(info));
  return 0;
}

static NativeSymbol g_pm_metal_externals_native_symbols[] = {
  { "pm_metal_external_count", (void *)pm_metal_external_count_native, "()I", NULL },
  { "pm_metal_external_get", (void *)pm_metal_external_get_native, "(ii)i", NULL },
  { "pm_metal_external_find", (void *)pm_metal_external_find_native, "($i)i", NULL },
};

int pm_metal_externals_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_EXTERNALS_WASI_MODULE,
                                     g_pm_metal_externals_native_symbols,
                                     sizeof(g_pm_metal_externals_native_symbols) /
                                       sizeof(g_pm_metal_externals_native_symbols[0]))) {
    return -1;
  }
  return 0;
}

#endif /* !__wasm__ */
