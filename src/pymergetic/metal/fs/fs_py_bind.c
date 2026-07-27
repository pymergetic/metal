/** @file
  pymergetic.metal.fs — sync fd-shaped file I/O for Python (backs
  mods/py/stdlib_src/os/ + io.py; see docs/MICROPYTHON.md). This build has
  no MICROPY_VFS/uos (Metal FS is its own async-shaped thing, not a mounted
  VFS), so os.py/io.py are Metal's own, not micropython-lib's uos-based
  ones — this is the whole surface they're written against, one real C
  function per Python call, never a string-keyed dispatch.
**/
#include <string.h>

#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>
#include <pymergetic/metal/runtime/mem/mem.h>

/* Only MicroPython header this file needs — see async_py_bind.c's comment;
 * mp_obj_str_get_str (path/mode/name-as-Python-str args) lives here too. */
#include "py/obj.h"

/** 0xffffffff = unrecognized mode string (caller raises ValueError). */
static uint32_t FsModeFlags(const char *mode)
{
  if (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0) {
    return PM_METAL_FS_O_RDONLY;
  }
  if (strcmp(mode, "w") == 0 || strcmp(mode, "wb") == 0) {
    return PM_METAL_FS_O_WRONLY | PM_METAL_FS_O_CREAT | PM_METAL_FS_O_TRUNC;
  }
  if (strcmp(mode, "a") == 0 || strcmp(mode, "ab") == 0) {
    return PM_METAL_FS_O_WRONLY | PM_METAL_FS_O_CREAT | PM_METAL_FS_O_APPEND;
  }
  if (strcmp(mode, "r+") == 0 || strcmp(mode, "r+b") == 0) {
    return PM_METAL_FS_O_RDWR;
  }
  if (strcmp(mode, "w+") == 0 || strcmp(mode, "w+b") == 0) {
    return PM_METAL_FS_O_RDWR | PM_METAL_FS_O_CREAT | PM_METAL_FS_O_TRUNC;
  }
  return 0xffffffffu;
}

static mp_obj_t py_fs_open(mp_obj_t path_obj, mp_obj_t mode_obj)
{
  uint32_t      flags = FsModeFlags(mp_obj_str_get_str(mode_obj));
  pm_metal_fs_h h;

  if (flags == 0xffffffffu) {
    pm_metal_py_raise_value_error("fs.open: bad mode");
  }

  h = pm_metal_fs_open(mp_obj_str_get_str(path_obj), flags);
  return pm_metal_py_int_new((h == PM_METAL_FS_INVALID) ? -1 : (int64_t)h);
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_fs_open_obj, py_fs_open);
PM_METAL_PY_BIND_DOC(g_py_bind_fs_open,
                    "pymergetic.metal.fs",
                    "open",
                    py_fs_open_obj,
                    PM_METAL_PY_SYNC,
                    "Open a Metal FS path, returning a small-int file handle (-1 on failure).",
                    "open(path: str, mode: str) -> int",
                    "mode is one of 'r'/'rb', 'w'/'wb', 'a'/'ab', 'r+'/'r+b', 'w+'/'w+b' -- "
                    "same subset os.py's own open() accepts (io.py wraps this into a "
                    "file-like object; most callers should use that instead of this "
                    "directly).");

static mp_obj_t py_fs_close(mp_obj_t h_obj)
{
  pm_metal_fs_close((pm_metal_fs_h)pm_metal_py_int_get(h_obj));
  return pm_metal_py_obj_none();
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_fs_close_obj, py_fs_close);
PM_METAL_PY_BIND(
  g_py_bind_fs_close, "pymergetic.metal.fs", "close", py_fs_close_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_fs_read(mp_obj_t h_obj, mp_obj_t n_obj)
{
  pm_metal_fs_h     h = (pm_metal_fs_h)pm_metal_py_int_get(h_obj);
  int64_t           n = pm_metal_py_int_get(n_obj);
  uint8_t           stack_buf[512];
  uint8_t          *buf;
  uint32_t          nread;
  pm_metal_py_obj_t out;

  if (n < 0) {
    pm_metal_py_raise_value_error("fs.read: n must be >= 0");
  }

  buf = stack_buf;
  if ((uint64_t)n > sizeof(stack_buf)) {
    buf = (uint8_t *)pm_metal_mem_alloc((uint32_t)n, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (buf == NULL) {
      pm_metal_py_raise_runtime_error("fs.read: alloc failed");
    }
  }

  nread = pm_metal_fs_fread(h, buf, (uint32_t)n);
  out   = pm_metal_py_bytes_new(buf, nread);
  if (buf != stack_buf) {
    pm_metal_mem_free(buf);
  }

  return out;
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_fs_read_obj, py_fs_read);
PM_METAL_PY_BIND(
  g_py_bind_fs_read, "pymergetic.metal.fs", "read", py_fs_read_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_fs_write(mp_obj_t h_obj, mp_obj_t data_obj)
{
  pm_metal_fs_h  h = (pm_metal_fs_h)pm_metal_py_int_get(h_obj);
  const uint8_t *buf;
  size_t         len;

  (void)pm_metal_py_buf_get(data_obj, &buf, &len);
  return pm_metal_py_int_new((int64_t)pm_metal_fs_fwrite(h, buf, (uint32_t)len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_fs_write_obj, py_fs_write);
PM_METAL_PY_BIND(
  g_py_bind_fs_write, "pymergetic.metal.fs", "write", py_fs_write_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_fs_lseek(mp_obj_t h_obj, mp_obj_t off_obj, mp_obj_t whence_obj)
{
  int32_t r = pm_metal_fs_lseek((pm_metal_fs_h)pm_metal_py_int_get(h_obj),
                                (int32_t)pm_metal_py_int_get(off_obj),
                                (uint32_t)pm_metal_py_int_get(whence_obj));
  return pm_metal_py_int_new(r);
}
static MP_DEFINE_CONST_FUN_OBJ_3(py_fs_lseek_obj, py_fs_lseek);
PM_METAL_PY_BIND(
  g_py_bind_fs_lseek, "pymergetic.metal.fs", "lseek", py_fs_lseek_obj, PM_METAL_PY_SYNC);

/** (type, size) — PM_METAL_FS_TYPE_FILE/_DIR, or None if missing. Python's
 * os.py builds the POSIX-shaped st_mode bits from `type` itself. */
static mp_obj_t py_fs_stat(mp_obj_t path_obj)
{
  pm_metal_fs_stat_t st;
  pm_metal_py_obj_t  items[2];

  if (pm_metal_fs_stat(mp_obj_str_get_str(path_obj), &st) != 0) {
    return pm_metal_py_obj_none();
  }

  items[0] = pm_metal_py_int_new((int64_t)st.type);
  items[1] = pm_metal_py_int_new((int64_t)st.size);
  return pm_metal_py_tuple_new(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_fs_stat_obj, py_fs_stat);
PM_METAL_PY_BIND(
  g_py_bind_fs_stat, "pymergetic.metal.fs", "stat", py_fs_stat_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_fs_mkdir(mp_obj_t path_obj)
{
  return pm_metal_py_int_new(pm_metal_fs_mkdir(mp_obj_str_get_str(path_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_fs_mkdir_obj, py_fs_mkdir);
PM_METAL_PY_BIND(
  g_py_bind_fs_mkdir, "pymergetic.metal.fs", "mkdir", py_fs_mkdir_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_fs_unlink(mp_obj_t path_obj)
{
  return pm_metal_py_int_new(pm_metal_fs_unlink(mp_obj_str_get_str(path_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_fs_unlink_obj, py_fs_unlink);
PM_METAL_PY_BIND(
  g_py_bind_fs_unlink, "pymergetic.metal.fs", "unlink", py_fs_unlink_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_fs_rename(mp_obj_t old_obj, mp_obj_t new_obj)
{
  return pm_metal_py_int_new(
    pm_metal_fs_rename(mp_obj_str_get_str(old_obj), mp_obj_str_get_str(new_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_fs_rename_obj, py_fs_rename);
PM_METAL_PY_BIND(
  g_py_bind_fs_rename, "pymergetic.metal.fs", "rename", py_fs_rename_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_fs_listdir(mp_obj_t path_obj)
{
  pm_metal_fs_h     h = pm_metal_fs_open(mp_obj_str_get_str(path_obj), PM_METAL_FS_O_DIRECTORY);
  pm_metal_py_obj_t list;
  char              name[128];

  if (h == PM_METAL_FS_INVALID) {
    pm_metal_py_raise_value_error("listdir: no such directory");
  }

  list = pm_metal_py_list_new();
  while (pm_metal_fs_readdir(h, name, sizeof(name)) > 0) {
    pm_metal_py_list_append(list, pm_metal_py_str_new(name));
  }

  pm_metal_fs_close(h);
  return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_fs_listdir_obj, py_fs_listdir);
PM_METAL_PY_BIND(
  g_py_bind_fs_listdir, "pymergetic.metal.fs", "listdir", py_fs_listdir_obj, PM_METAL_PY_SYNC);
