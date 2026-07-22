/** @file
  Metal FS — sync helpers, fd handles, awaitable ops. (impl: efi|bios)
**/
#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/fs/esp/esp.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/mem/mem.h>
#include <runtime/coro/coro.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>

#include "wasm_export.h"

#define PM_METAL_FS_HANDLE_MAX  32u

typedef struct {
  INT32   used;
  INT32   is_dir;
  CHAR8   path[256];
  UINT32  flags;
  UINT32  offset;
  UINT32  size;
  UINT32  dir_idx;
} metal_fs_handle_t;

typedef enum {
  PM_METAL_FS_OP_SIZE = 0,
  PM_METAL_FS_OP_READ,
  PM_METAL_FS_OP_WRITE,
  PM_METAL_FS_OP_OPEN,
  PM_METAL_FS_OP_CLOSE,
  PM_METAL_FS_OP_FREAD,
  PM_METAL_FS_OP_FWRITE,
  PM_METAL_FS_OP_FPREAD,
  PM_METAL_FS_OP_FPWRITE,
  PM_METAL_FS_OP_STAT,
  PM_METAL_FS_OP_FSTAT,
  PM_METAL_FS_OP_READDIR,
  PM_METAL_FS_OP_MKDIR,
  PM_METAL_FS_OP_UNLINK,
  PM_METAL_FS_OP_RENAME,
  PM_METAL_FS_OP_FSYNC
} pm_metal_fs_op_t;

typedef struct {
  pm_metal_coro_t   coro;
  pm_metal_fs_op_t  op;
  pm_metal_fs_h     fh;
  CHAR8             path[256];
  CHAR8             path2[256];
  uintptr_t         u0;
  uintptr_t         u1;
  uintptr_t         u2;
} pm_metal_fs_coro_t;

STATIC wasm_module_inst_t  mFsInst;
STATIC metal_fs_handle_t     mHandles[PM_METAL_FS_HANDLE_MAX];

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

  while (o > 0 && cleaned[o - 1] == '/') {
    o--;
  }

  cleaned[o] = '\0';
  return (cleaned[0] == '\0') ? -1 : 0;
}

STATIC
INT32
MetalFsGuestPathNative (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *path,
  CHAR8           *out,
  UINTN            out_sz
  )
{
  wasm_module_inst_t  inst;
  UINTN               i;

  inst = wasm_runtime_get_module_inst (exec_env);
  if (inst == NULL || path == NULL || out == NULL || out_sz == 0) {
    return -1;
  }

  if (!wasm_runtime_validate_native_addr (inst, (VOID *)path, 1)) {
    return -1;
  }

  for (i = 0; i + 1 < out_sz; i++) {
    if (!wasm_runtime_validate_native_addr (inst, (VOID *)(path + i), 1)) {
      return -1;
    }

    out[i] = path[i];
    if (path[i] == '\0') {
      return 0;
    }
  }

  return -1;
}

STATIC
VOID *
MetalFsGuestBufNative (
  wasm_exec_env_t  exec_env,
  UINT32           off,
  UINT32           len
  )
{
  wasm_module_inst_t  inst;

  if (len == 0 || off == 0) {
    return NULL;
  }

  inst = wasm_runtime_get_module_inst (exec_env);
  if (inst == NULL) {
    return NULL;
  }

  if (!wasm_runtime_validate_app_addr (inst, off, len)) {
    return NULL;
  }

  return wasm_runtime_addr_app_to_native (inst, off);
}

STATIC
metal_fs_handle_t *
MetalFsHandleAt (
  pm_metal_fs_h  h
  )
{
  if (h >= PM_METAL_FS_HANDLE_MAX || !mHandles[h].used) {
    return NULL;
  }

  return &mHandles[h];
}

STATIC
pm_metal_fs_h
MetalFsHandleAlloc (
  CONST CHAR8  *path,
  INT32         is_dir,
  UINT32        flags,
  UINT32        size
  )
{
  UINTN  i;

  for (i = 0; i < PM_METAL_FS_HANDLE_MAX; i++) {
    if (!mHandles[i].used) {
      ZeroMem (&mHandles[i], sizeof (mHandles[i]));
      mHandles[i].used    = 1;
      mHandles[i].is_dir  = is_dir;
      mHandles[i].flags   = flags;
      mHandles[i].size    = size;
      mHandles[i].dir_idx = 0;
      if ((flags & PM_METAL_FS_O_APPEND) != 0 && !is_dir) {
        mHandles[i].offset = size;
      }

      AsciiStrnCpyS (
        mHandles[i].path,
        sizeof (mHandles[i].path),
        path,
        sizeof (mHandles[i].path) - 1
        );
      return (pm_metal_fs_h)i;
    }
  }

  return PM_METAL_FS_INVALID;
}

STATIC
VOID
MetalFsHandleFree (
  pm_metal_fs_h  h
  )
{
  metal_fs_handle_t  *fh;

  fh = MetalFsHandleAt (h);
  if (fh == NULL) {
    return;
  }

  ZeroMem (fh, sizeof (*fh));
}

STATIC
INT32
MetalFsStatFill (
  CONST CHAR8  *path,
  VOID         *dest
  )
{
  pm_metal_fs_stat_t  st;
  UINT32              size;
  UINT32              type;

  if (dest == NULL || path == NULL || !pm_metal_esp_ready ()) {
    return 0;
  }

  if (pm_metal_esp_stat (path, &size, &type) != 0) {
    return 0;
  }

  ZeroMem (&st, sizeof (st));
  st.size = size;
  st.type = (type == PM_METAL_ESP_TYPE_DIR) ? PM_METAL_FS_TYPE_DIR
                                            : PM_METAL_FS_TYPE_FILE;

  CopyMem (dest, &st, sizeof (st));
  return 1;
}

STATIC
INT32
MetalFsOpenPath (
  CONST CHAR8  *path,
  UINT32        flags,
  pm_metal_fs_h *out
  )
{
  UINT32         size;
  UINT32         type;
  INT32          is_dir;
  pm_metal_fs_h  h;

  if (out == NULL || path == NULL || !pm_metal_esp_ready ()) {
    return -1;
  }

  *out   = PM_METAL_FS_INVALID;
  is_dir = ((flags & PM_METAL_FS_O_DIRECTORY) != 0) ? 1 : 0;

  if (is_dir) {
    if (pm_metal_esp_stat (path, &size, &type) != 0) {
      if ((flags & PM_METAL_FS_O_CREAT) == 0
          || pm_metal_esp_mkdir (path) != 0)
      {
        return -1;
      }

      type = PM_METAL_ESP_TYPE_DIR;
    } else if (type != PM_METAL_ESP_TYPE_DIR) {
      return -1;
    }

    h = MetalFsHandleAlloc (path, 1, flags, 0);
    if (h == PM_METAL_FS_INVALID) {
      return -1;
    }

    *out = h;
    return 0;
  }

  if (pm_metal_esp_stat (path, &size, &type) != 0) {
    if ((flags & PM_METAL_FS_O_CREAT) == 0) {
      return -1;
    }

    if (pm_metal_esp_write_at (path, 0, NULL, 0, 1) != 0) {
      return -1;
    }

    size = 0;
  } else if (type == PM_METAL_ESP_TYPE_DIR) {
    return -1;
  }

  if ((flags & PM_METAL_FS_O_TRUNC) != 0) {
    if (pm_metal_esp_write_at (path, 0, NULL, 0, 1) != 0) {
      return -1;
    }

    size = 0;
  }

  h = MetalFsHandleAlloc (path, 0, flags, size);
  if (h == PM_METAL_FS_INVALID) {
    return -1;
  }

  *out = h;
  return 0;
}

STATIC
UINT32
MetalFsReadHandle (
  pm_metal_fs_h  h,
  VOID          *dest,
  UINT32         len,
  INT32          advance
  )
{
  metal_fs_handle_t  *fh;
  UINT8               stack_buf[512];
  UINT8              *host;
  UINT32              nread;
  UINT32              copy;

  fh = MetalFsHandleAt (h);
  if (fh == NULL || fh->is_dir || dest == NULL || len == 0) {
    return 0;
  }

  host = stack_buf;
  if (len > sizeof (stack_buf)) {
    host = (UINT8 *)pm_metal_mem_alloc (
                      len,
                      PM_METAL_MEM_HEAP,
                      PM_METAL_MEM_ID_NONE
                      );
    if (host == NULL) {
      return 0;
    }
  }

  if (pm_metal_esp_read_at (fh->path, fh->offset, host, len, &nread) != 0) {
    if (host != stack_buf) {
      pm_metal_mem_free (host);
    }

    return 0;
  }

  copy = nread;
  CopyMem (dest, host, copy);
  if (host != stack_buf) {
    pm_metal_mem_free (host);
  }

  if (advance) {
    fh->offset += copy;
  }

  return copy;
}

STATIC
UINT32
MetalFsPreadHandle (
  pm_metal_fs_h  h,
  UINT32         off,
  VOID          *dest,
  UINT32         len
  )
{
  metal_fs_handle_t  *fh;
  UINT8               stack_buf[512];
  UINT8              *host;
  UINT32              nread;

  fh = MetalFsHandleAt (h);
  if (fh == NULL || fh->is_dir || dest == NULL || len == 0) {
    return 0;
  }

  host = stack_buf;
  if (len > sizeof (stack_buf)) {
    host = (UINT8 *)pm_metal_mem_alloc (
                      len,
                      PM_METAL_MEM_HEAP,
                      PM_METAL_MEM_ID_NONE
                      );
    if (host == NULL) {
      return 0;
    }
  }

  if (pm_metal_esp_read_at (fh->path, off, host, len, &nread) != 0) {
    if (host != stack_buf) {
      pm_metal_mem_free (host);
    }

    return 0;
  }

  CopyMem (dest, host, nread);
  if (host != stack_buf) {
    pm_metal_mem_free (host);
  }

  return nread;
}

STATIC
UINT32
MetalFsWriteHandle (
  pm_metal_fs_h   h,
  CONST VOID     *src,
  UINT32          len,
  INT32           advance
  )
{
  metal_fs_handle_t  *fh;

  fh = MetalFsHandleAt (h);
  if (fh == NULL || fh->is_dir) {
    return 0;
  }

  if (len > 0) {
    if (src == NULL) {
      return 0;
    }

    if (pm_metal_esp_write_at (fh->path, fh->offset, (CONST UINT8 *)src, len, 0) != 0) {
      return 0;
    }
  }

  if (advance) {
    fh->offset += len;
  }

  if (fh->offset > fh->size) {
    fh->size = fh->offset;
  }

  return len;
}

STATIC
UINT32
MetalFsPwriteHandle (
  pm_metal_fs_h   h,
  UINT32          off,
  CONST VOID     *src,
  UINT32          len
  )
{
  metal_fs_handle_t  *fh;

  fh = MetalFsHandleAt (h);
  if (fh == NULL || fh->is_dir) {
    return 0;
  }

  if (len > 0) {
    if (src == NULL) {
      return 0;
    }

    if (pm_metal_esp_write_at (fh->path, off, (CONST UINT8 *)src, len, 0) != 0) {
      return 0;
    }
  }

  if (off + len > fh->size) {
    fh->size = off + len;
  }

  return len;
}

STATIC
UINT32
MetalFsReaddirHandle (
  pm_metal_fs_h  h,
  CHAR8         *name_dest,
  UINT32         name_cap
  )
{
  metal_fs_handle_t  *fh;
  CHAR8               name[128];
  INT32               rc;

  fh = MetalFsHandleAt (h);
  if (fh == NULL || !fh->is_dir || name_dest == NULL || name_cap == 0) {
    return 0;
  }

  rc = pm_metal_esp_readdir (fh->path, fh->dir_idx, name, sizeof (name));
  if (rc <= 0) {
    return 0;
  }

  AsciiStrnCpyS (name_dest, name_cap, name, name_cap - 1);
  fh->dir_idx++;
  return (UINT32)AsciiStrLen (name);
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
  VOID         *dest,
  uint32_t      dest_len
  )
{
  UINT8  *host;
  UINT32  len;
  UINT32  copy_len;
  CHAR8   cleaned[256];

  if (path == NULL || dest == NULL || dest_len == 0 || !pm_metal_esp_ready ()) {
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

  CopyMem (dest, host, copy_len);
  pm_metal_mem_free (host);
  return copy_len;
}

uint32_t
pm_metal_fs_write (
  CONST CHAR8   *path,
  CONST VOID    *src,
  uint32_t       src_len
  )
{
  CHAR8  cleaned[256];

  if (path == NULL || !pm_metal_esp_ready ()) {
    return 0;
  }

  if (src_len > 0 && src == NULL) {
    return 0;
  }

  if (MetalFsCleanPath (path, cleaned, sizeof (cleaned)) != 0) {
    return 0;
  }

  if (pm_metal_esp_write_file (cleaned, (CONST UINT8 *)src, src_len) != 0) {
    return 0;
  }

  return src_len;
}

int32_t
pm_metal_fs_lseek (
  pm_metal_fs_h  h,
  int32_t        off,
  uint32_t       whence
  )
{
  metal_fs_handle_t  *fh;
  INT64              pos;

  fh = MetalFsHandleAt (h);
  if (fh == NULL || fh->is_dir) {
    return -1;
  }

  if (whence == PM_METAL_FS_SEEK_SET) {
    pos = off;
  } else if (whence == PM_METAL_FS_SEEK_CUR) {
    pos = (INT64)fh->offset + (INT64)off;
  } else if (whence == PM_METAL_FS_SEEK_END) {
    pos = (INT64)fh->size + (INT64)off;
  } else {
    return -1;
  }

  if (pos < 0) {
    return -1;
  }

  fh->offset = (UINT32)pos;
  return (int32_t)fh->offset;
}

STATIC
pm_metal_status_t
MetalFsCoroFn (
  pm_metal_coro_t  *self
  )
{
  pm_metal_fs_coro_t  *f;
  pm_metal_fs_h        opened;
  UINT32               n;

  f = (pm_metal_fs_coro_t *)self;
  n = 0;

  switch (f->op) {
  case PM_METAL_FS_OP_SIZE:
    n = pm_metal_fs_size (f->path);
    break;
  case PM_METAL_FS_OP_READ:
    n = pm_metal_fs_read (f->path, (VOID *)(UINTN)f->u0, (uint32_t)f->u1);
    break;
  case PM_METAL_FS_OP_WRITE:
    n = pm_metal_fs_write (f->path, (CONST VOID *)(UINTN)f->u0, (uint32_t)f->u1);
    break;
  case PM_METAL_FS_OP_OPEN:
    if (MetalFsOpenPath (f->path, (uint32_t)f->u0, &opened) != 0) {
      n = PM_METAL_FS_INVALID;
    } else {
      n = opened;
    }

    break;
  case PM_METAL_FS_OP_CLOSE:
    MetalFsHandleFree (f->fh);
    n = 1;
    break;
  case PM_METAL_FS_OP_FREAD:
    n = MetalFsReadHandle (f->fh, (VOID *)(UINTN)f->u0, (uint32_t)f->u1, 1);
    break;
  case PM_METAL_FS_OP_FWRITE:
    n = MetalFsWriteHandle (f->fh, (CONST VOID *)(UINTN)f->u0, (uint32_t)f->u1, 1);
    break;
  case PM_METAL_FS_OP_FPREAD:
    n = MetalFsPreadHandle (f->fh, (uint32_t)f->u0, (VOID *)(UINTN)f->u1,
                            (uint32_t)f->u2);
    break;
  case PM_METAL_FS_OP_FPWRITE:
    n = MetalFsPwriteHandle (f->fh, (uint32_t)f->u0, (CONST VOID *)(UINTN)f->u1,
                             (uint32_t)f->u2);
    break;
  case PM_METAL_FS_OP_STAT:
    n = MetalFsStatFill (f->path, (VOID *)(UINTN)f->u0);
    break;
  case PM_METAL_FS_OP_FSTAT:
    {
      metal_fs_handle_t  *fh;

      fh = MetalFsHandleAt (f->fh);
      if (fh == NULL) {
        n = 0;
      } else {
        n = MetalFsStatFill (fh->path, (VOID *)(UINTN)f->u0);
      }
    }

    break;
  case PM_METAL_FS_OP_READDIR:
    n = MetalFsReaddirHandle (f->fh, (CHAR8 *)(UINTN)f->u0, (uint32_t)f->u1);
    break;
  case PM_METAL_FS_OP_MKDIR:
    n = (pm_metal_esp_mkdir (f->path) == 0) ? 1u : 0u;
    break;
  case PM_METAL_FS_OP_UNLINK:
    n = (pm_metal_esp_unlink (f->path) == 0) ? 1u : 0u;
    break;
  case PM_METAL_FS_OP_RENAME:
    n = (pm_metal_esp_rename (f->path, f->path2) == 0) ? 1u : 0u;
    break;
  case PM_METAL_FS_OP_FSYNC:
    {
      metal_fs_handle_t  *fh;

      fh = MetalFsHandleAt (f->fh);
      if (fh == NULL || fh->is_dir) {
        n = 0;
      } else {
        n = (pm_metal_esp_fsync (fh->path) == 0) ? 1u : 0u;
      }
    }

    break;
  default:
    n = 0;
    break;
  }

  self->result = (VOID *)(UINTN)n;
  return PM_METAL_DONE;
}

STATIC
pm_metal_async_handle_t
MetalFsStartOp (
  pm_metal_fs_coro_t  *tmpl
  )
{
  pm_metal_fs_coro_t  *f;

  f = (pm_metal_fs_coro_t *)pm_metal_coro (
                              MetalFsCoroFn,
                              sizeof (*f)
                              );
  if (f == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  f->op     = tmpl->op;
  f->fh     = tmpl->fh;
  f->u0     = tmpl->u0;
  f->u1     = tmpl->u1;
  f->u2     = tmpl->u2;
  AsciiStrnCpyS (f->path, sizeof (f->path), tmpl->path, sizeof (f->path) - 1);
  AsciiStrnCpyS (f->path2, sizeof (f->path2), tmpl->path2, sizeof (f->path2) - 1);
  return pm_metal_async_adopt_host_coro (&f->coro);
}

STATIC
pm_metal_async_handle_t
MetalFsStartPath (
  pm_metal_fs_op_t  op,
  CONST CHAR8      *path,
  uintptr_t         u0,
  uintptr_t         u1
  )
{
  pm_metal_fs_coro_t  tmpl;
  CHAR8                cleaned[256];

  if (!pm_metal_esp_ready ()) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (MetalFsCleanPath (path, cleaned, sizeof (cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  ZeroMem (&tmpl, sizeof (tmpl));
  tmpl.op = op;
  tmpl.u0 = u0;
  tmpl.u1 = u1;
  AsciiStrnCpyS (tmpl.path, sizeof (tmpl.path), cleaned, sizeof (tmpl.path) - 1);
  return MetalFsStartOp (&tmpl);
}

STATIC
pm_metal_async_handle_t
MetalFsStartHandle (
  pm_metal_fs_op_t  op,
  pm_metal_fs_h     h,
  uintptr_t         u0,
  uintptr_t         u1,
  uintptr_t         u2
  )
{
  pm_metal_fs_coro_t  tmpl;

  if (!pm_metal_esp_ready () || MetalFsHandleAt (h) == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  ZeroMem (&tmpl, sizeof (tmpl));
  tmpl.op = op;
  tmpl.fh = h;
  tmpl.u0 = u0;
  tmpl.u1 = u1;
  tmpl.u2 = u2;
  return MetalFsStartOp (&tmpl);
}

pm_metal_async_handle_t
pm_metal_fs_open_async (
  CONST CHAR8  *path,
  uint32_t      flags
  )
{
  return MetalFsStartPath (PM_METAL_FS_OP_OPEN, path, flags, 0);
}

pm_metal_async_handle_t
pm_metal_fs_close_async (
  pm_metal_fs_h  h
  )
{
  return MetalFsStartHandle (PM_METAL_FS_OP_CLOSE, h, 0, 0, 0);
}

pm_metal_async_handle_t
pm_metal_fs_fread_async (
  pm_metal_fs_h  h,
  VOID          *dest,
  uint32_t       len
  )
{
  if (dest == NULL || len == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartHandle (PM_METAL_FS_OP_FREAD, h, (uintptr_t)dest, len, 0);
}

pm_metal_async_handle_t
pm_metal_fs_fwrite_async (
  pm_metal_fs_h   h,
  CONST VOID     *src,
  uint32_t        len
  )
{
  if (len > 0 && src == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartHandle (PM_METAL_FS_OP_FWRITE, h, (uintptr_t)src, len, 0);
}

pm_metal_async_handle_t
pm_metal_fs_fpread_async (
  pm_metal_fs_h  h,
  uint32_t       off,
  VOID          *dest,
  uint32_t       len
  )
{
  if (dest == NULL || len == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartHandle (PM_METAL_FS_OP_FPREAD, h, off, (uintptr_t)dest, len);
}

pm_metal_async_handle_t
pm_metal_fs_fpwrite_async (
  pm_metal_fs_h   h,
  uint32_t        off,
  CONST VOID     *src,
  uint32_t        len
  )
{
  if (len > 0 && src == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartHandle (PM_METAL_FS_OP_FPWRITE, h, off, (uintptr_t)src, len);
}

pm_metal_async_handle_t
pm_metal_fs_stat_async (
  CONST CHAR8  *path,
  VOID         *dest
  )
{
  if (dest == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartPath (PM_METAL_FS_OP_STAT, path, (uintptr_t)dest, 0);
}

pm_metal_async_handle_t
pm_metal_fs_fstat_async (
  pm_metal_fs_h  h,
  VOID          *dest
  )
{
  if (dest == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartHandle (PM_METAL_FS_OP_FSTAT, h, (uintptr_t)dest, 0, 0);
}

pm_metal_async_handle_t
pm_metal_fs_readdir_async (
  pm_metal_fs_h  h,
  CHAR8         *name_dest,
  uint32_t       name_cap
  )
{
  if (name_dest == NULL || name_cap == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartHandle (PM_METAL_FS_OP_READDIR, h, (uintptr_t)name_dest,
                             name_cap, 0);
}

pm_metal_async_handle_t
pm_metal_fs_mkdir_async (
  CONST CHAR8  *path
  )
{
  return MetalFsStartPath (PM_METAL_FS_OP_MKDIR, path, 0, 0);
}

pm_metal_async_handle_t
pm_metal_fs_unlink_async (
  CONST CHAR8  *path
  )
{
  return MetalFsStartPath (PM_METAL_FS_OP_UNLINK, path, 0, 0);
}

pm_metal_async_handle_t
pm_metal_fs_rename_async (
  CONST CHAR8  *old_path,
  CONST CHAR8  *new_path
  )
{
  pm_metal_fs_coro_t  tmpl;
  CHAR8                old_clean[256];
  CHAR8                new_clean[256];

  if (!pm_metal_esp_ready ()) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (MetalFsCleanPath (old_path, old_clean, sizeof (old_clean)) != 0
      || MetalFsCleanPath (new_path, new_clean, sizeof (new_clean)) != 0)
  {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  ZeroMem (&tmpl, sizeof (tmpl));
  tmpl.op = PM_METAL_FS_OP_RENAME;
  AsciiStrnCpyS (tmpl.path, sizeof (tmpl.path), old_clean, sizeof (tmpl.path) - 1);
  AsciiStrnCpyS (
    tmpl.path2,
    sizeof (tmpl.path2),
    new_clean,
    sizeof (tmpl.path2) - 1
    );
  return MetalFsStartOp (&tmpl);
}

pm_metal_async_handle_t
pm_metal_fs_fsync_async (
  pm_metal_fs_h  h
  )
{
  return MetalFsStartHandle (PM_METAL_FS_OP_FSYNC, h, 0, 0, 0);
}

pm_metal_async_handle_t
pm_metal_fs_size_async (
  CONST CHAR8  *path
  )
{
  return MetalFsStartPath (PM_METAL_FS_OP_SIZE, path, 0, 0);
}

pm_metal_async_handle_t
pm_metal_fs_read_async (
  CONST CHAR8  *path,
  VOID         *dest,
  uint32_t      dest_len
  )
{
  if (dest == NULL || dest_len == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartPath (PM_METAL_FS_OP_READ, path, (uintptr_t)dest, dest_len);
}

pm_metal_async_handle_t
pm_metal_fs_write_async (
  CONST CHAR8   *path,
  CONST VOID    *src,
  uint32_t       src_len
  )
{
  if (src_len > 0 && src == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartPath (PM_METAL_FS_OP_WRITE, path, (uintptr_t)src, src_len);
}

uint32_t
pm_metal_fs_result (
  pm_metal_async_handle_t  self_h
  )
{
  return pm_metal_async_result_u32 (self_h);
}

#define PM_METAL_FS_NATIVE(name, sig, fn) \
  { name, (VOID *)(fn), sig, NULL }

STATIC UINT32
pm_metal_fs_size_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *path
  )
{
  CHAR8  cleaned[256];

  if (MetalFsGuestPathNative (exec_env, path, cleaned, sizeof (cleaned)) != 0) {
    return 0;
  }

  return pm_metal_fs_size (cleaned);
}

STATIC UINT32
pm_metal_fs_read_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *path,
  UINT32           dest,
  UINT32           dest_len
  )
{
  CHAR8  cleaned[256];
  VOID  *native;

  if (MetalFsGuestPathNative (exec_env, path, cleaned, sizeof (cleaned)) != 0) {
    return 0;
  }

  native = MetalFsGuestBufNative (exec_env, dest, dest_len);
  if (native == NULL) {
    return 0;
  }

  return pm_metal_fs_read (cleaned, native, dest_len);
}

STATIC UINT32
pm_metal_fs_write_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *path,
  UINT32           src,
  UINT32           src_len
  )
{
  CHAR8       cleaned[256];
  CONST VOID  *native;

  if (MetalFsGuestPathNative (exec_env, path, cleaned, sizeof (cleaned)) != 0) {
    return 0;
  }

  native = NULL;
  if (src_len > 0) {
    native = MetalFsGuestBufNative (exec_env, src, src_len);
    if (native == NULL) {
      return 0;
    }
  }

  return pm_metal_fs_write (cleaned, native, src_len);
}

STATIC UINT32
pm_metal_fs_open_async_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *path,
  UINT32           flags
  )
{
  CHAR8  cleaned[256];

  if (MetalFsGuestPathNative (exec_env, path, cleaned, sizeof (cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_open_async (cleaned, flags);
}

STATIC UINT32
pm_metal_fs_close_async_native (
  wasm_exec_env_t  exec_env,
  UINT32           h
  )
{
  (VOID)exec_env;
  return pm_metal_fs_close_async ((pm_metal_fs_h)h);
}

STATIC UINT32
pm_metal_fs_fread_async_native (
  wasm_exec_env_t  exec_env,
  UINT32           h,
  UINT32           dest,
  UINT32           len
  )
{
  VOID  *native;

  native = MetalFsGuestBufNative (exec_env, dest, len);
  if (native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_fread_async ((pm_metal_fs_h)h, native, len);
}

STATIC UINT32
pm_metal_fs_fwrite_async_native (
  wasm_exec_env_t  exec_env,
  UINT32           h,
  UINT32           src,
  UINT32           len
  )
{
  CONST VOID  *native;

  native = NULL;
  if (len > 0) {
    native = MetalFsGuestBufNative (exec_env, src, len);
    if (native == NULL) {
      return PM_METAL_ASYNC_HANDLE_INVALID;
    }
  }

  return pm_metal_fs_fwrite_async ((pm_metal_fs_h)h, native, len);
}

STATIC UINT32
pm_metal_fs_fpread_async_native (
  wasm_exec_env_t  exec_env,
  UINT32           h,
  UINT32           off,
  UINT32           dest,
  UINT32           len
  )
{
  VOID  *native;

  native = MetalFsGuestBufNative (exec_env, dest, len);
  if (native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_fpread_async ((pm_metal_fs_h)h, off, native, len);
}

STATIC UINT32
pm_metal_fs_fpwrite_async_native (
  wasm_exec_env_t  exec_env,
  UINT32           h,
  UINT32           off,
  UINT32           src,
  UINT32           len
  )
{
  CONST VOID  *native;

  native = NULL;
  if (len > 0) {
    native = MetalFsGuestBufNative (exec_env, src, len);
    if (native == NULL) {
      return PM_METAL_ASYNC_HANDLE_INVALID;
    }
  }

  return pm_metal_fs_fpwrite_async ((pm_metal_fs_h)h, off, native, len);
}

STATIC INT32
pm_metal_fs_lseek_native (
  wasm_exec_env_t  exec_env,
  UINT32           h,
  INT32            off,
  UINT32           whence
  )
{
  (VOID)exec_env;
  return pm_metal_fs_lseek ((pm_metal_fs_h)h, off, whence);
}

STATIC UINT32
pm_metal_fs_stat_async_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *path,
  UINT32           dest
  )
{
  CHAR8  cleaned[256];
  VOID  *native;

  if (MetalFsGuestPathNative (exec_env, path, cleaned, sizeof (cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  native = MetalFsGuestBufNative (exec_env, dest, (UINT32)sizeof (pm_metal_fs_stat_t));
  if (native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_stat_async (cleaned, native);
}

STATIC UINT32
pm_metal_fs_fstat_async_native (
  wasm_exec_env_t  exec_env,
  UINT32           h,
  UINT32           dest
  )
{
  VOID  *native;

  native = MetalFsGuestBufNative (exec_env, dest, (UINT32)sizeof (pm_metal_fs_stat_t));
  if (native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_fstat_async ((pm_metal_fs_h)h, native);
}

STATIC UINT32
pm_metal_fs_readdir_async_native (
  wasm_exec_env_t  exec_env,
  UINT32           h,
  UINT32           name_dest,
  UINT32           name_cap
  )
{
  VOID  *native;

  native = MetalFsGuestBufNative (exec_env, name_dest, name_cap);
  if (native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_readdir_async ((pm_metal_fs_h)h, (CHAR8 *)native, name_cap);
}

STATIC UINT32
pm_metal_fs_mkdir_async_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *path
  )
{
  CHAR8  cleaned[256];

  if (MetalFsGuestPathNative (exec_env, path, cleaned, sizeof (cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_mkdir_async (cleaned);
}

STATIC UINT32
pm_metal_fs_unlink_async_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *path
  )
{
  CHAR8  cleaned[256];

  if (MetalFsGuestPathNative (exec_env, path, cleaned, sizeof (cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_unlink_async (cleaned);
}

STATIC UINT32
pm_metal_fs_rename_async_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *old_path,
  CONST CHAR8     *new_path
  )
{
  CHAR8  old_clean[256];
  CHAR8  new_clean[256];

  if (MetalFsGuestPathNative (exec_env, old_path, old_clean, sizeof (old_clean)) != 0
      || MetalFsGuestPathNative (exec_env, new_path, new_clean, sizeof (new_clean)) != 0)
  {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_rename_async (old_clean, new_clean);
}

STATIC UINT32
pm_metal_fs_fsync_async_native (
  wasm_exec_env_t  exec_env,
  UINT32           h
  )
{
  (VOID)exec_env;
  return pm_metal_fs_fsync_async ((pm_metal_fs_h)h);
}

STATIC UINT32
pm_metal_fs_size_async_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *path
  )
{
  CHAR8  cleaned[256];

  if (MetalFsGuestPathNative (exec_env, path, cleaned, sizeof (cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_size_async (cleaned);
}

STATIC UINT32
pm_metal_fs_read_async_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *path,
  UINT32           dest,
  UINT32           dest_len
  )
{
  CHAR8  cleaned[256];
  VOID  *native;

  if (MetalFsGuestPathNative (exec_env, path, cleaned, sizeof (cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  native = MetalFsGuestBufNative (exec_env, dest, dest_len);
  if (native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_read_async (cleaned, native, dest_len);
}

STATIC UINT32
pm_metal_fs_write_async_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *path,
  UINT32           src,
  UINT32           src_len
  )
{
  CHAR8       cleaned[256];
  CONST VOID  *native;

  if (MetalFsGuestPathNative (exec_env, path, cleaned, sizeof (cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  native = NULL;
  if (src_len > 0) {
    native = MetalFsGuestBufNative (exec_env, src, src_len);
    if (native == NULL) {
      return PM_METAL_ASYNC_HANDLE_INVALID;
    }
  }

  return pm_metal_fs_write_async (cleaned, native, src_len);
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
  PM_METAL_FS_NATIVE ("pm_metal_fs_open_async", "($i)i", pm_metal_fs_open_async_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_close_async", "(i)i", pm_metal_fs_close_async_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_fread_async", "(iii)i", pm_metal_fs_fread_async_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_fwrite_async", "(iii)i", pm_metal_fs_fwrite_async_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_fpread_async", "(iiii)i", pm_metal_fs_fpread_async_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_fpwrite_async", "(iiii)i", pm_metal_fs_fpwrite_async_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_lseek", "(iii)i", pm_metal_fs_lseek_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_stat_async", "($i)i", pm_metal_fs_stat_async_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_fstat_async", "(ii)i", pm_metal_fs_fstat_async_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_readdir_async", "(iii)i", pm_metal_fs_readdir_async_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_mkdir_async", "($)i", pm_metal_fs_mkdir_async_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_unlink_async", "($)i", pm_metal_fs_unlink_async_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_rename_async", "($$)i", pm_metal_fs_rename_async_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_fsync_async", "(i)i", pm_metal_fs_fsync_async_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_size_async", "($)i", pm_metal_fs_size_async_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_read_async", "($ii)i", pm_metal_fs_read_async_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_write_async", "($ii)i", pm_metal_fs_write_async_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_result", "(i)i", pm_metal_fs_result_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_size", "($)i", pm_metal_fs_size_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_read", "($ii)i", pm_metal_fs_read_native),
  PM_METAL_FS_NATIVE ("pm_metal_fs_write", "($ii)i", pm_metal_fs_write_native),
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
