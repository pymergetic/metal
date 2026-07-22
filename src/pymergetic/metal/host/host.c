/** @file
  Local hostname identity (nodename). Not DNS resolution.
**/
#include <pymergetic/metal/host/host.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/dev/net/net_cfg.h>

#include <Uefi.h>
#include <Library/BaseLib.h>

#include "wasm_export.h"

STATIC CHAR8  mHostName[PM_METAL_HOST_NAME_MAX] = "metal";

STATIC
INT32
HostNameValid (
  const char  *name
  )
{
  size_t  i;
  char    c;

  if (name == NULL || name[0] == '\0') {
    return 0;
  }

  for (i = 0; name[i] != '\0'; i++) {
    if (i + 1u >= PM_METAL_HOST_NAME_MAX) {
      return 0;
    }

    c = name[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '-' || c == '.')
    {
      continue;
    }

    return 0;
  }

  if (name[0] == '-' || name[0] == '.' || name[i - 1] == '-'
      || name[i - 1] == '.')
  {
    return 0;
  }

  return 1;
}

int
pm_metal_host_name_get (
  char    *out,
  size_t   cap
  )
{
  if (out == NULL || cap == 0) {
    return -1;
  }

  if (AsciiStrCpyS (out, cap, mHostName) != RETURN_SUCCESS) {
    return -1;
  }

  return 0;
}

int
pm_metal_host_name_set (
  const char  *name
  )
{
  if (!HostNameValid (name)) {
    return -1;
  }

  if (AsciiStrCmp (mHostName, name) == 0) {
    return 0;
  }

  if (AsciiStrCpyS (mHostName, sizeof (mHostName), name) != RETURN_SUCCESS) {
    return -1;
  }

  pm_metal_net_on_hostname_changed ();
  return 0;
}

const char *
pm_metal_host_name_cstr (
  VOID
  )
{
  return mHostName;
}

STATIC
VOID
HostnameShellCmd (
  int    argc,
  char **argv
  )
{
  char  name[PM_METAL_HOST_NAME_MAX];

  if (argc < 2) {
    if (pm_metal_host_name_get (name, sizeof (name)) == 0) {
      pm_metal_shell_out (name);
    }

    return;
  }

  if (pm_metal_host_name_set (argv[1]) != 0) {
    pm_metal_shell_out ("hostname: invalid name");
    return;
  }

  if (pm_metal_host_name_get (name, sizeof (name)) == 0) {
    pm_metal_shell_out (name);
  }
}

PM_METAL_SHELL_CMD (
  g_pm_metal_shell_cmd_hostname,
  "hostname",
  "hostname [name]   get/set local nodename (not DNS)",
  HostnameShellCmd
  );

STATIC
INT32
pm_metal_host_name_get_native (
  wasm_exec_env_t  exec_env,
  char            *out,
  UINT32           cap
  )
{
  (VOID)exec_env;
  return (INT32)pm_metal_host_name_get (out, (size_t)cap);
}

STATIC
INT32
pm_metal_host_name_set_native (
  wasm_exec_env_t  exec_env,
  char            *name
  )
{
  (VOID)exec_env;
  return (INT32)pm_metal_host_name_set (name);
}

STATIC NativeSymbol g_pm_metal_host_native_symbols[] = {
  { "pm_metal_host_name_get", (VOID *)pm_metal_host_name_get_native, "(*~)i",
    NULL },
  { "pm_metal_host_name_set", (VOID *)pm_metal_host_name_set_native, "($)i",
    NULL },
};

int
pm_metal_host_native_register (
  VOID
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_HOST_WASI_MODULE,
         g_pm_metal_host_native_symbols,
         sizeof (g_pm_metal_host_native_symbols)
           / sizeof (g_pm_metal_host_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}
