/** @file
  Metal FS — sync helpers, fd handles, awaitable ops. (impl: efi|bios)
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/fs/esp/esp.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/mem/mem_internal.h>

#include "wasm_export.h"

#define PM_METAL_FS_HANDLE_MAX 32u

typedef struct {
  int32_t  used;
  int32_t  is_dir;
  char     path[256];
  uint32_t flags;
  uint32_t offset;
  uint32_t size;
  uint32_t dir_idx;
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
  pm_metal_fs_op_t op;
  pm_metal_fs_h    fh;
  char             path[256];
  char             path2[256];
  uintptr_t        u0;
  uintptr_t        u1;
  uintptr_t        u2;
} pm_metal_fs_coro_t;

static wasm_module_inst_t mFsInst;
static metal_fs_handle_t  mHandles[PM_METAL_FS_HANDLE_MAX];

void pm_metal_fs_bind_inst(void *module_inst)
{
  mFsInst = (wasm_module_inst_t)module_inst;
}

static int32_t MetalFsCleanPath(const char *path, char *cleaned, uintptr_t cleaned_sz)
{
  uintptr_t i;
  uintptr_t o;

  if (path == NULL || cleaned == NULL || cleaned_sz == 0) {
    return -1;
  }

  o = 0;
  i = 0;
  while (path[i] == '/' || path[i] == '\\') {
    i++;
  }

  while (path[i] != '\0' && o + 1 < cleaned_sz) {
    char c;

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

static int32_t MetalFsGuestPathNative(wasm_exec_env_t exec_env,
                                      const char     *path,
                                      char           *out,
                                      uintptr_t       out_sz)
{
  wasm_module_inst_t inst;
  uintptr_t          i;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || path == NULL || out == NULL || out_sz == 0) {
    return -1;
  }

  if (!wasm_runtime_validate_native_addr(inst, (void *)path, 1)) {
    return -1;
  }

  for (i = 0; i + 1 < out_sz; i++) {
    if (!wasm_runtime_validate_native_addr(inst, (void *)(path + i), 1)) {
      return -1;
    }

    out[i] = path[i];
    if (path[i] == '\0') {
      return 0;
    }
  }

  return -1;
}

static void *MetalFsGuestBufNative(wasm_exec_env_t exec_env, uint32_t off, uint32_t len)
{
  return pm_metal_async_guest_buf_durable(exec_env, off, len);
}

static metal_fs_handle_t *MetalFsHandleAt(pm_metal_fs_h h)
{
  if (h >= PM_METAL_FS_HANDLE_MAX || !mHandles[h].used) {
    return NULL;
  }

  return &mHandles[h];
}

static pm_metal_fs_h MetalFsHandleAlloc(const char *path,
                                        int32_t     is_dir,
                                        uint32_t    flags,
                                        uint32_t    size)
{
  uintptr_t i;

  for (i = 0; i < PM_METAL_FS_HANDLE_MAX; i++) {
    if (!mHandles[i].used) {
      memset(&mHandles[i], 0, sizeof(mHandles[i]));
      mHandles[i].used    = 1;
      mHandles[i].is_dir  = is_dir;
      mHandles[i].flags   = flags;
      mHandles[i].size    = size;
      mHandles[i].dir_idx = 0;
      if ((flags & PM_METAL_FS_O_APPEND) != 0 && !is_dir) {
        mHandles[i].offset = size;
      }

      snprintf(mHandles[i].path,
               sizeof(mHandles[i].path),
               "%.*s",
               (int)(sizeof(mHandles[i].path) - 1),
               path);
      return (pm_metal_fs_h)i;
    }
  }

  return PM_METAL_FS_INVALID;
}

static void MetalFsHandleFree(pm_metal_fs_h h)
{
  metal_fs_handle_t *fh;

  fh = MetalFsHandleAt(h);
  if (fh == NULL) {
    return;
  }

  memset(fh, 0, sizeof(*fh));
}

static int32_t MetalFsStatFill(const char *path, void *dest)
{
  pm_metal_fs_stat_t st;
  uint32_t           size;
  uint32_t           type;

  if (dest == NULL || path == NULL || !pm_metal_esp_ready()) {
    return 0;
  }

  if (pm_metal_esp_stat(path, &size, &type) != 0) {
    return 0;
  }

  memset(&st, 0, sizeof(st));
  st.size = size;
  st.type = (type == PM_METAL_ESP_TYPE_DIR) ? PM_METAL_FS_TYPE_DIR : PM_METAL_FS_TYPE_FILE;

  memcpy(dest, &st, sizeof(st));
  return 1;
}

static int32_t MetalFsOpenPath(const char *path, uint32_t flags, pm_metal_fs_h *out)
{
  uint32_t      size;
  uint32_t      type;
  int32_t       is_dir;
  pm_metal_fs_h h;

  if (out == NULL || path == NULL || !pm_metal_esp_ready()) {
    return -1;
  }

  *out   = PM_METAL_FS_INVALID;
  is_dir = ((flags & PM_METAL_FS_O_DIRECTORY) != 0) ? 1 : 0;

  if (is_dir) {
    if (pm_metal_esp_stat(path, &size, &type) != 0) {
      if ((flags & PM_METAL_FS_O_CREAT) == 0 || pm_metal_esp_mkdir(path) != 0) {
        return -1;
      }

      type = PM_METAL_ESP_TYPE_DIR;
    } else if (type != PM_METAL_ESP_TYPE_DIR) {
      return -1;
    }

    h = MetalFsHandleAlloc(path, 1, flags, 0);
    if (h == PM_METAL_FS_INVALID) {
      return -1;
    }

    *out = h;
    return 0;
  }

  if (pm_metal_esp_stat(path, &size, &type) != 0) {
    if ((flags & PM_METAL_FS_O_CREAT) == 0) {
      return -1;
    }

    if (pm_metal_esp_write_at(path, 0, NULL, 0, 1) != 0) {
      return -1;
    }

    size = 0;
  } else if (type == PM_METAL_ESP_TYPE_DIR) {
    return -1;
  }

  if ((flags & PM_METAL_FS_O_TRUNC) != 0) {
    if (pm_metal_esp_write_at(path, 0, NULL, 0, 1) != 0) {
      return -1;
    }

    size = 0;
  }

  h = MetalFsHandleAlloc(path, 0, flags, size);
  if (h == PM_METAL_FS_INVALID) {
    return -1;
  }

  *out = h;
  return 0;
}

static uint32_t MetalFsReadHandle(pm_metal_fs_h h, void *dest, uint32_t len, int32_t advance)
{
  metal_fs_handle_t *fh;
  uint8_t            stack_buf[512];
  uint8_t           *host;
  uint32_t           nread;
  uint32_t           copy;

  fh = MetalFsHandleAt(h);
  if (fh == NULL || fh->is_dir || dest == NULL || len == 0) {
    return 0;
  }

  host = stack_buf;
  if (len > sizeof(stack_buf)) {
    host = (uint8_t *)pm_metal_mem_alloc(len, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (host == NULL) {
      return 0;
    }
  }

  if (pm_metal_esp_read_at(fh->path, fh->offset, host, len, &nread) != 0) {
    if (host != stack_buf) {
      pm_metal_mem_free(host);
    }

    return 0;
  }

  copy = nread;
  memcpy(dest, host, copy);
  if (host != stack_buf) {
    pm_metal_mem_free(host);
  }

  if (advance) {
    fh->offset += copy;
  }

  return copy;
}

static uint32_t MetalFsPreadHandle(pm_metal_fs_h h, uint32_t off, void *dest, uint32_t len)
{
  metal_fs_handle_t *fh;
  uint8_t            stack_buf[512];
  uint8_t           *host;
  uint32_t           nread;

  fh = MetalFsHandleAt(h);
  if (fh == NULL || fh->is_dir || dest == NULL || len == 0) {
    return 0;
  }

  host = stack_buf;
  if (len > sizeof(stack_buf)) {
    host = (uint8_t *)pm_metal_mem_alloc(len, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (host == NULL) {
      return 0;
    }
  }

  if (pm_metal_esp_read_at(fh->path, off, host, len, &nread) != 0) {
    if (host != stack_buf) {
      pm_metal_mem_free(host);
    }

    return 0;
  }

  memcpy(dest, host, nread);
  if (host != stack_buf) {
    pm_metal_mem_free(host);
  }

  return nread;
}

static uint32_t MetalFsWriteHandle(pm_metal_fs_h h, const void *src, uint32_t len, int32_t advance)
{
  metal_fs_handle_t *fh;

  fh = MetalFsHandleAt(h);
  if (fh == NULL || fh->is_dir) {
    return 0;
  }

  if (len > 0) {
    if (src == NULL) {
      return 0;
    }

    if (pm_metal_esp_write_at(fh->path, fh->offset, (const uint8_t *)src, len, 0) != 0) {
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

static uint32_t MetalFsPwriteHandle(pm_metal_fs_h h, uint32_t off, const void *src, uint32_t len)
{
  metal_fs_handle_t *fh;

  fh = MetalFsHandleAt(h);
  if (fh == NULL || fh->is_dir) {
    return 0;
  }

  if (len > 0) {
    if (src == NULL) {
      return 0;
    }

    if (pm_metal_esp_write_at(fh->path, off, (const uint8_t *)src, len, 0) != 0) {
      return 0;
    }
  }

  if (off + len > fh->size) {
    fh->size = off + len;
  }

  return len;
}

static uint32_t MetalFsReaddirHandle(pm_metal_fs_h h, char *name_dest, uint32_t name_cap)
{
  metal_fs_handle_t *fh;
  char               name[128];
  int32_t            rc;

  fh = MetalFsHandleAt(h);
  if (fh == NULL || !fh->is_dir || name_dest == NULL || name_cap == 0) {
    return 0;
  }

  rc = pm_metal_esp_readdir(fh->path, fh->dir_idx, name, sizeof(name));
  if (rc <= 0) {
    return 0;
  }

  snprintf(name_dest, name_cap, "%.*s", (int)(name_cap - 1), name);
  fh->dir_idx++;
  return (uint32_t)strlen(name);
}

uint32_t pm_metal_fs_size(const char *path)
{
  uint32_t len;
  char     cleaned[256];

  if (!pm_metal_esp_ready()) {
    return 0;
  }

  if (MetalFsCleanPath(path, cleaned, sizeof(cleaned)) != 0) {
    return 0;
  }

  len = 0;
  if (pm_metal_esp_file_size(cleaned, &len) != 0) {
    return 0;
  }

  return len;
}

uint32_t pm_metal_fs_read(const char *path, void *dest, uint32_t dest_len)
{
  uint32_t nread;
  char     cleaned[256];

  if (path == NULL || dest == NULL || dest_len == 0 || !pm_metal_esp_ready()) {
    return 0;
  }

  if (MetalFsCleanPath(path, cleaned, sizeof(cleaned)) != 0) {
    return 0;
  }

  /*
   * Copy ESP/VFS/cache bytes straight into the caller buffer (guest linear
   * memory for wasm). Do not allocate a second full-file host copy — that
   * OOMs on multi-MiB IWADs and made doom look like a guest malloc bug.
   */
  nread = 0;
  if (pm_metal_esp_read_at(cleaned, 0, (uint8_t *)dest, dest_len, &nread) != 0) {
    return 0;
  }

  return nread;
}

uint32_t pm_metal_fs_write(const char *path, const void *src, uint32_t src_len)
{
  char cleaned[256];

  if (path == NULL || !pm_metal_esp_ready()) {
    return 0;
  }

  if (src_len > 0 && src == NULL) {
    return 0;
  }

  if (MetalFsCleanPath(path, cleaned, sizeof(cleaned)) != 0) {
    return 0;
  }

  if (pm_metal_esp_write_file(cleaned, (const uint8_t *)src, src_len) != 0) {
    return 0;
  }

  return src_len;
}

int32_t pm_metal_fs_lseek(pm_metal_fs_h h, int32_t off, uint32_t whence)
{
  metal_fs_handle_t *fh;
  int64_t            pos;

  fh = MetalFsHandleAt(h);
  if (fh == NULL || fh->is_dir) {
    return -1;
  }

  if (whence == PM_METAL_FS_SEEK_SET) {
    pos = off;
  } else if (whence == PM_METAL_FS_SEEK_CUR) {
    pos = (int64_t)fh->offset + (int64_t)off;
  } else if (whence == PM_METAL_FS_SEEK_END) {
    pos = (int64_t)fh->size + (int64_t)off;
  } else {
    return -1;
  }

  if (pos < 0) {
    return -1;
  }

  fh->offset = (uint32_t)pos;
  return (int32_t)fh->offset;
}

static pm_metal_status_t MetalFsStep(pm_metal_async_handle_t self_h)
{
  pm_metal_fs_coro_t *f;
  pm_metal_fs_h       opened;
  uint32_t            n;

  f = (pm_metal_fs_coro_t *)(uintptr_t)pm_metal_async_coro_state(self_h);
  if (f == NULL) {
    return PM_METAL_ERROR;
  }

  n = 0;

  switch (f->op) {
  case PM_METAL_FS_OP_SIZE:
    n = pm_metal_fs_size(f->path);
    break;
  case PM_METAL_FS_OP_READ:
    n = pm_metal_fs_read(f->path, (void *)(uintptr_t)f->u0, (uint32_t)f->u1);
    break;
  case PM_METAL_FS_OP_WRITE:
    n = pm_metal_fs_write(f->path, (const void *)(uintptr_t)f->u0, (uint32_t)f->u1);
    break;
  case PM_METAL_FS_OP_OPEN:
    if (MetalFsOpenPath(f->path, (uint32_t)f->u0, &opened) != 0) {
      n = PM_METAL_FS_INVALID;
    } else {
      n = opened;
    }

    break;
  case PM_METAL_FS_OP_CLOSE:
    MetalFsHandleFree(f->fh);
    n = 1;
    break;
  case PM_METAL_FS_OP_FREAD:
    n = MetalFsReadHandle(f->fh, (void *)(uintptr_t)f->u0, (uint32_t)f->u1, 1);
    break;
  case PM_METAL_FS_OP_FWRITE:
    n = MetalFsWriteHandle(f->fh, (const void *)(uintptr_t)f->u0, (uint32_t)f->u1, 1);
    break;
  case PM_METAL_FS_OP_FPREAD:
    n = MetalFsPreadHandle(f->fh, (uint32_t)f->u0, (void *)(uintptr_t)f->u1, (uint32_t)f->u2);
    break;
  case PM_METAL_FS_OP_FPWRITE:
    n =
      MetalFsPwriteHandle(f->fh, (uint32_t)f->u0, (const void *)(uintptr_t)f->u1, (uint32_t)f->u2);
    break;
  case PM_METAL_FS_OP_STAT:
    n = MetalFsStatFill(f->path, (void *)(uintptr_t)f->u0);
    break;
  case PM_METAL_FS_OP_FSTAT: {
    metal_fs_handle_t *fh;

    fh = MetalFsHandleAt(f->fh);
    if (fh == NULL) {
      n = 0;
    } else {
      n = MetalFsStatFill(fh->path, (void *)(uintptr_t)f->u0);
    }
  }

  break;
  case PM_METAL_FS_OP_READDIR:
    n = MetalFsReaddirHandle(f->fh, (char *)(uintptr_t)f->u0, (uint32_t)f->u1);
    break;
  case PM_METAL_FS_OP_MKDIR:
    n = (pm_metal_esp_mkdir(f->path) == 0) ? 1u : 0u;
    break;
  case PM_METAL_FS_OP_UNLINK:
    n = (pm_metal_esp_unlink(f->path) == 0) ? 1u : 0u;
    break;
  case PM_METAL_FS_OP_RENAME:
    n = (pm_metal_esp_rename(f->path, f->path2) == 0) ? 1u : 0u;
    break;
  case PM_METAL_FS_OP_FSYNC: {
    metal_fs_handle_t *fh;

    fh = MetalFsHandleAt(f->fh);
    if (fh == NULL || fh->is_dir) {
      n = 0;
    } else {
      n = (pm_metal_esp_fsync(fh->path) == 0) ? 1u : 0u;
    }
  }

  break;
  default:
    n = 0;
    break;
  }

  pm_metal_async_set_result_u32(self_h, n);
  return PM_METAL_DONE;
}

static pm_metal_async_handle_t MetalFsStartOp(pm_metal_fs_coro_t *tmpl)
{
  pm_metal_fs_coro_t     *f;
  pm_metal_async_handle_t h;

  h = pm_metal_async_coro_create(MetalFsStep, sizeof(*f));
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  f = (pm_metal_fs_coro_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (f == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  f->op = tmpl->op;
  f->fh = tmpl->fh;
  f->u0 = tmpl->u0;
  f->u1 = tmpl->u1;
  f->u2 = tmpl->u2;
  snprintf(f->path, sizeof(f->path), "%.*s", (int)(sizeof(f->path) - 1), tmpl->path);
  snprintf(f->path2, sizeof(f->path2), "%.*s", (int)(sizeof(f->path2) - 1), tmpl->path2);
  return h;
}

static pm_metal_async_handle_t MetalFsStartPath(pm_metal_fs_op_t op,
                                                const char      *path,
                                                uintptr_t        u0,
                                                uintptr_t        u1)
{
  pm_metal_fs_coro_t tmpl;
  char               cleaned[256];

  if (!pm_metal_esp_ready()) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (MetalFsCleanPath(path, cleaned, sizeof(cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  memset(&tmpl, 0, sizeof(tmpl));
  tmpl.op = op;
  tmpl.u0 = u0;
  tmpl.u1 = u1;
  snprintf(tmpl.path, sizeof(tmpl.path), "%.*s", (int)(sizeof(tmpl.path) - 1), cleaned);
  return MetalFsStartOp(&tmpl);
}

static pm_metal_async_handle_t MetalFsStartHandle(
  pm_metal_fs_op_t op, pm_metal_fs_h h, uintptr_t u0, uintptr_t u1, uintptr_t u2)
{
  pm_metal_fs_coro_t tmpl;

  if (!pm_metal_esp_ready() || MetalFsHandleAt(h) == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  memset(&tmpl, 0, sizeof(tmpl));
  tmpl.op = op;
  tmpl.fh = h;
  tmpl.u0 = u0;
  tmpl.u1 = u1;
  tmpl.u2 = u2;
  return MetalFsStartOp(&tmpl);
}

pm_metal_async_handle_t pm_metal_fs_open_async(const char *path, uint32_t flags)
{
  return MetalFsStartPath(PM_METAL_FS_OP_OPEN, path, flags, 0);
}

pm_metal_async_handle_t pm_metal_fs_close_async(pm_metal_fs_h h)
{
  return MetalFsStartHandle(PM_METAL_FS_OP_CLOSE, h, 0, 0, 0);
}

pm_metal_async_handle_t pm_metal_fs_fread_async(pm_metal_fs_h h, void *dest, uint32_t len)
{
  if (dest == NULL || len == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartHandle(PM_METAL_FS_OP_FREAD, h, (uintptr_t)dest, len, 0);
}

pm_metal_async_handle_t pm_metal_fs_fwrite_async(pm_metal_fs_h h, const void *src, uint32_t len)
{
  if (len > 0 && src == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartHandle(PM_METAL_FS_OP_FWRITE, h, (uintptr_t)src, len, 0);
}

pm_metal_async_handle_t pm_metal_fs_fpread_async(pm_metal_fs_h h,
                                                 uint32_t      off,
                                                 void         *dest,
                                                 uint32_t      len)
{
  if (dest == NULL || len == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartHandle(PM_METAL_FS_OP_FPREAD, h, off, (uintptr_t)dest, len);
}

pm_metal_async_handle_t pm_metal_fs_fpwrite_async(pm_metal_fs_h h,
                                                  uint32_t      off,
                                                  const void   *src,
                                                  uint32_t      len)
{
  if (len > 0 && src == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartHandle(PM_METAL_FS_OP_FPWRITE, h, off, (uintptr_t)src, len);
}

pm_metal_async_handle_t pm_metal_fs_stat_async(const char *path, void *dest)
{
  if (dest == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartPath(PM_METAL_FS_OP_STAT, path, (uintptr_t)dest, 0);
}

pm_metal_async_handle_t pm_metal_fs_fstat_async(pm_metal_fs_h h, void *dest)
{
  if (dest == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartHandle(PM_METAL_FS_OP_FSTAT, h, (uintptr_t)dest, 0, 0);
}

pm_metal_async_handle_t pm_metal_fs_readdir_async(pm_metal_fs_h h,
                                                  char         *name_dest,
                                                  uint32_t      name_cap)
{
  if (name_dest == NULL || name_cap == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartHandle(PM_METAL_FS_OP_READDIR, h, (uintptr_t)name_dest, name_cap, 0);
}

pm_metal_async_handle_t pm_metal_fs_mkdir_async(const char *path)
{
  return MetalFsStartPath(PM_METAL_FS_OP_MKDIR, path, 0, 0);
}

pm_metal_async_handle_t pm_metal_fs_unlink_async(const char *path)
{
  return MetalFsStartPath(PM_METAL_FS_OP_UNLINK, path, 0, 0);
}

pm_metal_async_handle_t pm_metal_fs_rename_async(const char *old_path, const char *new_path)
{
  pm_metal_fs_coro_t tmpl;
  char               old_clean[256];
  char               new_clean[256];

  if (!pm_metal_esp_ready()) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (MetalFsCleanPath(old_path, old_clean, sizeof(old_clean)) != 0 ||
      MetalFsCleanPath(new_path, new_clean, sizeof(new_clean)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  memset(&tmpl, 0, sizeof(tmpl));
  tmpl.op = PM_METAL_FS_OP_RENAME;
  snprintf(tmpl.path, sizeof(tmpl.path), "%.*s", (int)(sizeof(tmpl.path) - 1), old_clean);
  snprintf(tmpl.path2, sizeof(tmpl.path2), "%.*s", (int)(sizeof(tmpl.path2) - 1), new_clean);
  return MetalFsStartOp(&tmpl);
}

pm_metal_async_handle_t pm_metal_fs_fsync_async(pm_metal_fs_h h)
{
  return MetalFsStartHandle(PM_METAL_FS_OP_FSYNC, h, 0, 0, 0);
}

pm_metal_async_handle_t pm_metal_fs_size_async(const char *path)
{
  return MetalFsStartPath(PM_METAL_FS_OP_SIZE, path, 0, 0);
}

pm_metal_async_handle_t pm_metal_fs_read_async(const char *path, void *dest, uint32_t dest_len)
{
  if (dest == NULL || dest_len == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartPath(PM_METAL_FS_OP_READ, path, (uintptr_t)dest, dest_len);
}

pm_metal_async_handle_t pm_metal_fs_write_async(const char *path, const void *src, uint32_t src_len)
{
  if (src_len > 0 && src == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return MetalFsStartPath(PM_METAL_FS_OP_WRITE, path, (uintptr_t)src, src_len);
}

pm_metal_async_handle_t pm_metal_fs_read_mem_async(const char    *path,
                                                   pm_metal_ptr_t dest,
                                                   uint32_t       dest_len)
{
  void *native;

  native = pm_metal_mem_guest_ptr(dest);
  if (native == NULL || dest_len == 0 || dest_len > pm_metal_mem_guest_size(dest)) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_read_async(path, native, dest_len);
}

pm_metal_async_handle_t pm_metal_fs_write_mem_async(const char    *path,
                                                    pm_metal_ptr_t src,
                                                    uint32_t       src_len)
{
  void *native;

  if (src_len == 0) {
    return pm_metal_fs_write_async(path, NULL, 0);
  }

  native = pm_metal_mem_guest_ptr(src);
  if (native == NULL || src_len > pm_metal_mem_guest_size(src)) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_write_async(path, native, src_len);
}

uint32_t pm_metal_fs_result(pm_metal_async_handle_t self_h)
{
  return pm_metal_async_result_u32(self_h);
}

#define PM_METAL_FS_NATIVE(name, sig, fn) \
  {                                       \
    name, (void *)(fn), sig, NULL         \
  }

static uint32_t pm_metal_fs_size_native(wasm_exec_env_t exec_env, const char *path)
{
  char cleaned[256];

  if (MetalFsGuestPathNative(exec_env, path, cleaned, sizeof(cleaned)) != 0) {
    return 0;
  }

  return pm_metal_fs_size(cleaned);
}

static uint32_t pm_metal_fs_read_native(wasm_exec_env_t exec_env,
                                        const char     *path,
                                        uint32_t        dest,
                                        uint32_t        dest_len)
{
  char  cleaned[256];
  void *native;

  if (MetalFsGuestPathNative(exec_env, path, cleaned, sizeof(cleaned)) != 0) {
    return 0;
  }

  native = MetalFsGuestBufNative(exec_env, dest, dest_len);
  if (native == NULL) {
    return 0;
  }

  return pm_metal_fs_read(cleaned, native, dest_len);
}

static uint32_t pm_metal_fs_write_native(wasm_exec_env_t exec_env,
                                         const char     *path,
                                         uint32_t        src,
                                         uint32_t        src_len)
{
  char        cleaned[256];
  const void *native;

  if (MetalFsGuestPathNative(exec_env, path, cleaned, sizeof(cleaned)) != 0) {
    return 0;
  }

  native = NULL;
  if (src_len > 0) {
    native = MetalFsGuestBufNative(exec_env, src, src_len);
    if (native == NULL) {
      return 0;
    }
  }

  return pm_metal_fs_write(cleaned, native, src_len);
}

static uint32_t pm_metal_fs_open_async_native(wasm_exec_env_t exec_env,
                                              const char     *path,
                                              uint32_t        flags)
{
  char cleaned[256];

  if (MetalFsGuestPathNative(exec_env, path, cleaned, sizeof(cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_open_async(cleaned, flags);
}

static uint32_t pm_metal_fs_close_async_native(wasm_exec_env_t exec_env, uint32_t h)
{
  (void)exec_env;
  return pm_metal_fs_close_async((pm_metal_fs_h)h);
}

static uint32_t pm_metal_fs_fread_async_native(wasm_exec_env_t exec_env,
                                               uint32_t        h,
                                               uint32_t        dest,
                                               uint32_t        len)
{
  void *native;

  native = MetalFsGuestBufNative(exec_env, dest, len);
  if (native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_fread_async((pm_metal_fs_h)h, native, len);
}

static uint32_t pm_metal_fs_fwrite_async_native(wasm_exec_env_t exec_env,
                                                uint32_t        h,
                                                uint32_t        src,
                                                uint32_t        len)
{
  const void *native;

  native = NULL;
  if (len > 0) {
    native = MetalFsGuestBufNative(exec_env, src, len);
    if (native == NULL) {
      return PM_METAL_ASYNC_HANDLE_INVALID;
    }
  }

  return pm_metal_fs_fwrite_async((pm_metal_fs_h)h, native, len);
}

static uint32_t pm_metal_fs_fpread_async_native(
  wasm_exec_env_t exec_env, uint32_t h, uint32_t off, uint32_t dest, uint32_t len)
{
  void *native;

  native = MetalFsGuestBufNative(exec_env, dest, len);
  if (native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_fpread_async((pm_metal_fs_h)h, off, native, len);
}

static uint32_t pm_metal_fs_fpwrite_async_native(
  wasm_exec_env_t exec_env, uint32_t h, uint32_t off, uint32_t src, uint32_t len)
{
  const void *native;

  native = NULL;
  if (len > 0) {
    native = MetalFsGuestBufNative(exec_env, src, len);
    if (native == NULL) {
      return PM_METAL_ASYNC_HANDLE_INVALID;
    }
  }

  return pm_metal_fs_fpwrite_async((pm_metal_fs_h)h, off, native, len);
}

static int32_t pm_metal_fs_lseek_native(wasm_exec_env_t exec_env,
                                        uint32_t        h,
                                        int32_t         off,
                                        uint32_t        whence)
{
  (void)exec_env;
  return pm_metal_fs_lseek((pm_metal_fs_h)h, off, whence);
}

static uint32_t pm_metal_fs_stat_async_native(wasm_exec_env_t exec_env,
                                              const char     *path,
                                              uint32_t        dest)
{
  char  cleaned[256];
  void *native;

  if (MetalFsGuestPathNative(exec_env, path, cleaned, sizeof(cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  native = MetalFsGuestBufNative(exec_env, dest, (uint32_t)sizeof(pm_metal_fs_stat_t));
  if (native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_stat_async(cleaned, native);
}

static uint32_t pm_metal_fs_fstat_async_native(wasm_exec_env_t exec_env, uint32_t h, uint32_t dest)
{
  void *native;

  native = MetalFsGuestBufNative(exec_env, dest, (uint32_t)sizeof(pm_metal_fs_stat_t));
  if (native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_fstat_async((pm_metal_fs_h)h, native);
}

static uint32_t pm_metal_fs_readdir_async_native(wasm_exec_env_t exec_env,
                                                 uint32_t        h,
                                                 uint32_t        name_dest,
                                                 uint32_t        name_cap)
{
  void *native;

  native = MetalFsGuestBufNative(exec_env, name_dest, name_cap);
  if (native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_readdir_async((pm_metal_fs_h)h, (char *)native, name_cap);
}

static uint32_t pm_metal_fs_mkdir_async_native(wasm_exec_env_t exec_env, const char *path)
{
  char cleaned[256];

  if (MetalFsGuestPathNative(exec_env, path, cleaned, sizeof(cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_mkdir_async(cleaned);
}

static uint32_t pm_metal_fs_unlink_async_native(wasm_exec_env_t exec_env, const char *path)
{
  char cleaned[256];

  if (MetalFsGuestPathNative(exec_env, path, cleaned, sizeof(cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_unlink_async(cleaned);
}

static uint32_t pm_metal_fs_rename_async_native(wasm_exec_env_t exec_env,
                                                const char     *old_path,
                                                const char     *new_path)
{
  char old_clean[256];
  char new_clean[256];

  if (MetalFsGuestPathNative(exec_env, old_path, old_clean, sizeof(old_clean)) != 0 ||
      MetalFsGuestPathNative(exec_env, new_path, new_clean, sizeof(new_clean)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_rename_async(old_clean, new_clean);
}

static uint32_t pm_metal_fs_fsync_async_native(wasm_exec_env_t exec_env, uint32_t h)
{
  (void)exec_env;
  return pm_metal_fs_fsync_async((pm_metal_fs_h)h);
}

static uint32_t pm_metal_fs_size_async_native(wasm_exec_env_t exec_env, const char *path)
{
  char cleaned[256];

  if (MetalFsGuestPathNative(exec_env, path, cleaned, sizeof(cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_size_async(cleaned);
}

static uint32_t pm_metal_fs_read_async_native(wasm_exec_env_t exec_env,
                                              const char     *path,
                                              uint32_t        dest,
                                              uint32_t        dest_len)
{
  char  cleaned[256];
  void *native;

  if (MetalFsGuestPathNative(exec_env, path, cleaned, sizeof(cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  native = MetalFsGuestBufNative(exec_env, dest, dest_len);
  if (native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_read_async(cleaned, native, dest_len);
}

static uint32_t pm_metal_fs_write_async_native(wasm_exec_env_t exec_env,
                                               const char     *path,
                                               uint32_t        src,
                                               uint32_t        src_len)
{
  char        cleaned[256];
  const void *native;

  if (MetalFsGuestPathNative(exec_env, path, cleaned, sizeof(cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  native = NULL;
  if (src_len > 0) {
    native = MetalFsGuestBufNative(exec_env, src, src_len);
    if (native == NULL) {
      return PM_METAL_ASYNC_HANDLE_INVALID;
    }
  }

  return pm_metal_fs_write_async(cleaned, native, src_len);
}

static uint32_t pm_metal_fs_read_mem_async_native(wasm_exec_env_t exec_env,
                                                  const char     *path,
                                                  uint32_t        dest_cookie,
                                                  uint32_t        dest_len)
{
  char cleaned[256];

  (void)exec_env;
  if (MetalFsGuestPathNative(exec_env, path, cleaned, sizeof(cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_read_mem_async(cleaned, (pm_metal_ptr_t)(uintptr_t)dest_cookie, dest_len);
}

static uint32_t pm_metal_fs_write_mem_async_native(wasm_exec_env_t exec_env,
                                                   const char     *path,
                                                   uint32_t        src_cookie,
                                                   uint32_t        src_len)
{
  char cleaned[256];

  (void)exec_env;
  if (MetalFsGuestPathNative(exec_env, path, cleaned, sizeof(cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_fs_write_mem_async(cleaned, (pm_metal_ptr_t)(uintptr_t)src_cookie, src_len);
}

static uint32_t pm_metal_fs_result_native(wasm_exec_env_t exec_env, uint32_t self_h)
{
  (void)exec_env;
  return pm_metal_fs_result(self_h);
}

static NativeSymbol g_pm_metal_fs_native_symbols[] = {
  PM_METAL_FS_NATIVE("pm_metal_fs_open_async", "($i)i", pm_metal_fs_open_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_close_async", "(i)i", pm_metal_fs_close_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_fread_async", "(iii)i", pm_metal_fs_fread_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_fwrite_async", "(iii)i", pm_metal_fs_fwrite_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_fpread_async", "(iiii)i", pm_metal_fs_fpread_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_fpwrite_async", "(iiii)i", pm_metal_fs_fpwrite_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_lseek", "(iii)i", pm_metal_fs_lseek_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_stat_async", "($i)i", pm_metal_fs_stat_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_fstat_async", "(ii)i", pm_metal_fs_fstat_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_readdir_async", "(iii)i", pm_metal_fs_readdir_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_mkdir_async", "($)i", pm_metal_fs_mkdir_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_unlink_async", "($)i", pm_metal_fs_unlink_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_rename_async", "($$)i", pm_metal_fs_rename_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_fsync_async", "(i)i", pm_metal_fs_fsync_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_size_async", "($)i", pm_metal_fs_size_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_read_async", "($ii)i", pm_metal_fs_read_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_write_async", "($ii)i", pm_metal_fs_write_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_read_mem_async", "($ii)i", pm_metal_fs_read_mem_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_write_mem_async", "($ii)i", pm_metal_fs_write_mem_async_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_result", "(i)i", pm_metal_fs_result_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_size", "($)i", pm_metal_fs_size_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_read", "($ii)i", pm_metal_fs_read_native),
  PM_METAL_FS_NATIVE("pm_metal_fs_write", "($ii)i", pm_metal_fs_write_native),
};

int pm_metal_fs_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_FS_WASI_MODULE,
                                     g_pm_metal_fs_native_symbols,
                                     sizeof(g_pm_metal_fs_native_symbols) /
                                       sizeof(g_pm_metal_fs_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
