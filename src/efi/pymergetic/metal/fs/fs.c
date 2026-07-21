/** @file
  ESP file load — sync helpers + awaitable FS ops. (impl: efi)
**/
#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/esp/esp.h>
#include <pymergetic/metal/async/async.h>
#include <mem/mem.h>
#include <coro/coro.h>

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
  UINT32  len;
  CHAR8   cleaned[256];

  if (!pm_metal_esp_ready ()) {
    return 0;
  }

  if (MetalFsCleanPath (path, cleaned, sizeof (cleaned)) != 0) {
    return 0;
  }

  len = 0;
  if (pm_metal_esp_file_size (cleaned, &len) != 0) {
    return 0;
  }

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
  if (pm_metal_esp_read_file (cleaned, &host, &len) != 0) {
    return 0;
  }

  /* Zero-length file: success with 0 bytes (host may be NULL). */
  if (len == 0) {
    return 0;
  }

  if (host == NULL) {
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

uint32_t
pm_metal_fs_write (
  CONST CHAR8  *path,
  uint32_t      src,
  uint32_t      src_len
  )
{
  CONST UINT8  *native;
  CHAR8         cleaned[256];

  if (mFsInst == NULL || path == NULL || !pm_metal_esp_ready ()) {
    return 0;
  }

  if (src_len > 0 && (src == 0
                      || !wasm_runtime_validate_app_addr (mFsInst, src, src_len)))
  {
    return 0;
  }

  if (MetalFsCleanPath (path, cleaned, sizeof (cleaned)) != 0) {
    return 0;
  }

  native = NULL;
  if (src_len > 0) {
    native = (CONST UINT8 *)wasm_runtime_addr_app_to_native (mFsInst, src);
    if (native == NULL) {
      return 0;
    }
  }

  if (pm_metal_esp_write_file (cleaned, native, src_len) != 0) {
    return 0;
  }

  return src_len;
}

/* ---- awaitable FS (eager ESP today; still always awaited) ---- */

typedef enum {
  PM_METAL_FS_OP_SIZE = 0,
  PM_METAL_FS_OP_READ,
  PM_METAL_FS_OP_WRITE
} pm_metal_fs_op_t;

typedef struct {
  pm_metal_coro_t   coro;
  pm_metal_fs_op_t  op;
  CHAR8             path[256];
  UINT32            dest;
  UINT32            dest_len;
} pm_metal_fs_coro_t;

STATIC
pm_metal_status_t
MetalFsCoroFn (
  pm_metal_coro_t  *self
  )
{
  pm_metal_fs_coro_t  *f;
  UINT32               n;

  f = (pm_metal_fs_coro_t *)self;
  n = 0;

  if (f->op == PM_METAL_FS_OP_SIZE) {
    n = pm_metal_fs_size (f->path);
  } else if (f->op == PM_METAL_FS_OP_READ) {
    n = pm_metal_fs_read (f->path, f->dest, f->dest_len);
  } else if (f->op == PM_METAL_FS_OP_WRITE) {
    n = pm_metal_fs_write (f->path, f->dest, f->dest_len);
  }

  self->result = (VOID *)(UINTN)n;
  return PM_METAL_DONE;
}

STATIC
pm_metal_async_handle_t
MetalFsStart (
  pm_metal_fs_op_t  op,
  CONST CHAR8      *path,
  UINT32            dest,
  UINT32            dest_len
  )
{
  pm_metal_fs_coro_t  *f;
  CHAR8                cleaned[256];

  if (MetalFsCleanPath (path, cleaned, sizeof (cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (op == PM_METAL_FS_OP_READ && (dest == 0 || dest_len == 0)) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (op == PM_METAL_FS_OP_WRITE && dest_len > 0 && dest == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  f = (pm_metal_fs_coro_t *)pm_metal_coro (
                              MetalFsCoroFn,
                              sizeof (*f)
                              );
  if (f == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  f->op       = op;
  f->dest     = dest;
  f->dest_len = dest_len;
  AsciiStrnCpyS (f->path, sizeof (f->path), cleaned, sizeof (f->path) - 1);
  return pm_metal_async_adopt_host_coro (&f->coro);
}

pm_metal_async_handle_t
pm_metal_fs_size_async (
  CONST CHAR8  *path
  )
{
  return MetalFsStart (PM_METAL_FS_OP_SIZE, path, 0, 0);
}

pm_metal_async_handle_t
pm_metal_fs_read_async (
  CONST CHAR8  *path,
  uint32_t      dest,
  uint32_t      dest_len
  )
{
  return MetalFsStart (PM_METAL_FS_OP_READ, path, dest, dest_len);
}

pm_metal_async_handle_t
pm_metal_fs_write_async (
  CONST CHAR8  *path,
  uint32_t      src,
  uint32_t      src_len
  )
{
  return MetalFsStart (PM_METAL_FS_OP_WRITE, path, src, src_len);
}

uint32_t
pm_metal_fs_result (
  pm_metal_async_handle_t  self_h
  )
{
  return pm_metal_async_result_u32 (self_h);
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

STATIC UINT32
pm_metal_fs_size_async_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *path
  )
{
  (VOID)exec_env;
  return pm_metal_fs_size_async (path);
}

STATIC UINT32
pm_metal_fs_read_async_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *path,
  UINT32           dest,
  UINT32           dest_len
  )
{
  (VOID)exec_env;
  return pm_metal_fs_read_async (path, dest, dest_len);
}

STATIC UINT32
pm_metal_fs_write_async_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *path,
  UINT32           src,
  UINT32           src_len
  )
{
  (VOID)exec_env;
  return pm_metal_fs_write_async (path, src, src_len);
}

STATIC UINT32
pm_metal_fs_result_native (
  wasm_exec_env_t  exec_env,
  UINT32           self_h
  )
{
  (VOID)exec_env;
  return pm_metal_fs_result (self_h);
}

STATIC NativeSymbol g_pm_metal_fs_native_symbols[] = {
  { "pm_metal_fs_size_async", (VOID *)pm_metal_fs_size_async_native, "($)i", NULL },
  { "pm_metal_fs_read_async", (VOID *)pm_metal_fs_read_async_native, "($ii)i", NULL },
  { "pm_metal_fs_write_async", (VOID *)pm_metal_fs_write_async_native, "($ii)i", NULL },
  { "pm_metal_fs_result", (VOID *)pm_metal_fs_result_native, "(i)i", NULL },
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
