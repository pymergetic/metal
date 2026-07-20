/** @file
  Load ESP files into guest linear memory (caller-provided buffer). (impl: efi)
**/
#include <pymergetic/metal/fs.h>
#include <pymergetic/metal/esp.h>
#include <mem/mem.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>

#include "wasm_export.h"

STATIC wasm_module_inst_t  mFsInst;

void
pm_metal_fs_bind_inst (
  VOID  *module_inst
  )
{
  mFsInst = (wasm_module_inst_t)module_inst;
}

STATIC
INT32
MetalFsCleanPath (
  CONST CHAR8  *path,
  CHAR8        *cleaned,
  UINTN         cleaned_sz
  )
{
  UINTN  i;
  UINTN  o;

  if (path == NULL || cleaned == NULL || cleaned_sz == 0) {
    return -1;
  }

  o = 0;
  i = 0;
  while (path[i] == '/' || path[i] == '\\') {
    i++;
  }

  while (path[i] != '\0' && o + 1 < cleaned_sz) {
    CHAR8  c;

    c = path[i++];
    if (c == '\\') {
      c = '/';
    }

    cleaned[o++] = c;
  }

  cleaned[o] = '\0';
  return (cleaned[0] == '\0') ? -1 : 0;
}

uint32_t
pm_metal_fs_size (
  CONST CHAR8  *path
  )
{
  UINT8  *host;
  UINT32  len;
  CHAR8   cleaned[256];

  if (mFsInst == NULL || !pm_metal_esp_ready ()) {
    return 0;
  }

  if (MetalFsCleanPath (path, cleaned, sizeof (cleaned)) != 0) {
    return 0;
  }

  host = NULL;
  len  = 0;
  if (pm_metal_esp_read_file (cleaned, &host, &len) != 0 || host == NULL) {
    return 0;
  }

  pm_metal_mem_free (host);
  return len;
}

uint32_t
pm_metal_fs_read (
  CONST CHAR8  *path,
  uint32_t      dest,
  uint32_t      dest_len
  )
{
  UINT8  *host;
  UINT32  len;
  UINT32  copy_len;
  VOID   *native;
  CHAR8   cleaned[256];

  if (mFsInst == NULL || path == NULL || dest == 0 || dest_len == 0
      || !pm_metal_esp_ready ())
  {
    return 0;
  }

  if (MetalFsCleanPath (path, cleaned, sizeof (cleaned)) != 0) {
    return 0;
  }

  host = NULL;
  len  = 0;
  if (pm_metal_esp_read_file (cleaned, &host, &len) != 0 || host == NULL) {
    return 0;
  }

  copy_len = len;
  if (copy_len > dest_len) {
    copy_len = dest_len;
  }

  if (!wasm_runtime_validate_app_addr (mFsInst, dest, copy_len)) {
    pm_metal_mem_free (host);
    return 0;
  }

  native = wasm_runtime_addr_app_to_native (mFsInst, dest);
  if (native == NULL) {
    pm_metal_mem_free (host);
    return 0;
  }

  CopyMem (native, host, copy_len);
  pm_metal_mem_free (host);
  return copy_len;
}

STATIC UINT32
pm_metal_fs_size_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *path
  )
{
  (VOID)exec_env;
  return pm_metal_fs_size (path);
}

STATIC UINT32
pm_metal_fs_read_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *path,
  UINT32           dest,
  UINT32           dest_len
  )
{
  (VOID)exec_env;
  return pm_metal_fs_read (path, dest, dest_len);
}

STATIC NativeSymbol g_pm_metal_fs_native_symbols[] = {
  { "pm_metal_fs_size", (VOID *)pm_metal_fs_size_native, "($)i", NULL },
  { "pm_metal_fs_read", (VOID *)pm_metal_fs_read_native, "($ii)i", NULL },
};

int
pm_metal_fs_native_register (
  VOID
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_FS_WASI_MODULE,
         g_pm_metal_fs_native_symbols,
         sizeof (g_pm_metal_fs_native_symbols)
           / sizeof (g_pm_metal_fs_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}
