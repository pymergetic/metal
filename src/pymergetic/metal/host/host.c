/** @file
  Local hostname identity (nodename). Not DNS resolution.
**/
#include <pymergetic/metal/host/host.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/dev/net/net_cfg.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wasm_export.h"

static char mHostName[PM_METAL_HOST_NAME_MAX] = "metal";

static int32_t HostNameValid(const char *name)
{
  size_t i;
  char   c;

  if (name == NULL || name[0] == '\0') {
    return 0;
  }

  for (i = 0; name[i] != '\0'; i++) {
    if (i + 1u >= PM_METAL_HOST_NAME_MAX) {
      return 0;
    }

    c = name[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '.') {
      continue;
    }

    return 0;
  }

  if (name[0] == '-' || name[0] == '.' || name[i - 1] == '-' || name[i - 1] == '.') {
    return 0;
  }

  return 1;
}

int pm_metal_host_name_get(char *out, size_t cap)
{
  int w;

  if (out == NULL || cap == 0) {
    return -1;
  }

  w = snprintf(out, cap, "%s", mHostName);
  if (w < 0 || (size_t)w >= cap) {
    return -1;
  }

  return 0;
}

int pm_metal_host_name_set(const char *name)
{
  int w;

  if (!HostNameValid(name)) {
    return -1;
  }

  if (strcmp(mHostName, name) == 0) {
    return 0;
  }

  w = snprintf(mHostName, sizeof(mHostName), "%s", name);
  if (w < 0 || (size_t)w >= sizeof(mHostName)) {
    return -1;
  }

  pm_metal_net_on_hostname_changed();
  return 0;
}

const char *pm_metal_host_name_cstr(void)
{
  return mHostName;
}

static void HostnameShellCmd(int argc, char **argv)
{
  char name[PM_METAL_HOST_NAME_MAX];

  if (argc < 2) {
    if (pm_metal_host_name_get(name, sizeof(name)) == 0) {
      pm_metal_shell_out(name);
    }

    return;
  }

  if (pm_metal_host_name_set(argv[1]) != 0) {
    pm_metal_shell_out("hostname: invalid name");
    return;
  }

  if (pm_metal_host_name_get(name, sizeof(name)) == 0) {
    pm_metal_shell_out(name);
  }
}

PM_METAL_SHELL_CMD(g_pm_metal_shell_cmd_hostname,
                   "hostname",
                   "hostname [name]   get/set local nodename (not DNS)",
                   HostnameShellCmd);

static int32_t pm_metal_host_name_get_native(wasm_exec_env_t exec_env, char *out, uint32_t cap)
{
  (void)exec_env;
  return (int32_t)pm_metal_host_name_get(out, (size_t)cap);
}

static int32_t pm_metal_host_name_set_native(wasm_exec_env_t exec_env, char *name)
{
  (void)exec_env;
  return (int32_t)pm_metal_host_name_set(name);
}

static NativeSymbol g_pm_metal_host_native_symbols[] = {
  { "pm_metal_host_name_get", (void *)pm_metal_host_name_get_native, "(*~)i", NULL },
  { "pm_metal_host_name_set", (void *)pm_metal_host_name_set_native, "($)i", NULL },
};

int pm_metal_host_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_HOST_WASI_MODULE,
                                     g_pm_metal_host_native_symbols,
                                     sizeof(g_pm_metal_host_native_symbols) /
                                       sizeof(g_pm_metal_host_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
