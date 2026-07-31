/** @file
  pymergetic.metal.tar — sync ustar reader/writer for Python (backs
  mods/py/stdlib/tarfile.py). Wraps util/tar.h's own iter_t/writer_t
  (host-only, buffer in/buffer out, no filesystem) — Metal's own facade,
  not micropython-lib's upstream `tarfile` package (that one needs
  uctypes, which this build doesn't carry; see docs/MICROPYTHON.md).

  util/tar.h's iter_init()/writer_init() don't copy the buffer they're
  given — the caller must keep it alive for the whole session. A Python
  bytes/bytearray's backing storage is only safe to hold a raw pointer
  into for the duration of one bind call (py_obj.h's buf_get() doc), not
  across the several separate Python-level calls a read session needs
  (open/next/name/read/...), so open_read() copies the archive into an
  owned host buffer up front; open_write() allocates its own scratch
  buffer the same way. Both are freed by close()/finish().
**/
#include <string.h>

#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <pymergetic/metal/util/tar.h>

#include "py/obj.h"

#define TAR_PY_MAX_SLOTS 16

typedef struct {
  int                        used;
  int                        is_writer;
  pm_metal_util_tar_iter_t   it;
  pm_metal_util_tar_writer_t wr;
  uint8_t                   *buf;
  size_t                     cap;
} tar_py_slot_t;

static tar_py_slot_t g_tar_py_slots[TAR_PY_MAX_SLOTS];

static int32_t TarSlotClaim(void)
{
  int32_t i;

  for (i = 0; i < TAR_PY_MAX_SLOTS; i++) {
    if (!g_tar_py_slots[i].used) {
      memset(&g_tar_py_slots[i], 0, sizeof(g_tar_py_slots[i]));
      g_tar_py_slots[i].used = 1;
      return i;
    }
  }

  return -1;
}

static tar_py_slot_t *TarSlotGet(int64_t h)
{
  int32_t idx = (int32_t)(h - 1);

  if (idx < 0 || idx >= TAR_PY_MAX_SLOTS || !g_tar_py_slots[idx].used) {
    return NULL;
  }

  return &g_tar_py_slots[idx];
}

static void TarSlotRelease(tar_py_slot_t *s)
{
  if (s == NULL) {
    return;
  }

  if (s->buf != NULL) {
    pm_metal_mem_free(s->buf);
  }

  memset(s, 0, sizeof(*s));
}

static mp_obj_t py_tar_open_read(mp_obj_t data_obj)
{
  const uint8_t *src;
  size_t         len;
  int32_t        idx;
  tar_py_slot_t *s;

  (void)pm_metal_py_buf_get(data_obj, &src, &len);

  idx = TarSlotClaim();
  if (idx < 0) {
    pm_metal_py_raise_runtime_error("tar: no free slots");
  }

  s = &g_tar_py_slots[idx];
  if (len > 0) {
    s->buf = (uint8_t *)pm_metal_mem_alloc((uint32_t)len, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (s->buf == NULL) {
      TarSlotRelease(s);
      pm_metal_py_raise_runtime_error("tar: alloc failed");
    }

    memcpy(s->buf, src, len);
  }

  s->cap = len;
  pm_metal_util_tar_iter_init(&s->it, s->buf, s->cap);
  return pm_metal_py_int_new((int64_t)idx + 1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_tar_open_read_obj, py_tar_open_read);
PM_METAL_PY_BIND(g_py_bind_tar_open_read,
                 "pymergetic.metal.tar",
                 "open_read",
                 py_tar_open_read_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t py_tar_next(mp_obj_t h_obj)
{
  tar_py_slot_t *s = TarSlotGet(pm_metal_py_int_get(h_obj));

  if (s == NULL) {
    pm_metal_py_raise_value_error("tar: bad handle");
  }

  return pm_metal_py_int_new((int64_t)pm_metal_util_tar_iter_next(&s->it));
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_tar_next_obj, py_tar_next);
PM_METAL_PY_BIND(
  g_py_bind_tar_next, "pymergetic.metal.tar", "next", py_tar_next_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_tar_name(mp_obj_t h_obj)
{
  tar_py_slot_t *s = TarSlotGet(pm_metal_py_int_get(h_obj));
  char           name[PM_METAL_UTIL_TAR_NAME_MAX];

  if (s == NULL) {
    pm_metal_py_raise_value_error("tar: bad handle");
  }

  if (pm_metal_util_tar_iter_name(&s->it, name, sizeof(name)) < 0) {
    pm_metal_py_raise_value_error("tar: no current entry");
  }

  return pm_metal_py_str_new(name);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_tar_name_obj, py_tar_name);
PM_METAL_PY_BIND(
  g_py_bind_tar_name, "pymergetic.metal.tar", "name", py_tar_name_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_tar_size(mp_obj_t h_obj)
{
  tar_py_slot_t *s = TarSlotGet(pm_metal_py_int_get(h_obj));

  if (s == NULL) {
    pm_metal_py_raise_value_error("tar: bad handle");
  }

  return pm_metal_py_int_new((int64_t)pm_metal_util_tar_iter_size(&s->it));
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_tar_size_obj, py_tar_size);
PM_METAL_PY_BIND(
  g_py_bind_tar_size, "pymergetic.metal.tar", "size", py_tar_size_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_tar_is_dir(mp_obj_t h_obj)
{
  tar_py_slot_t *s = TarSlotGet(pm_metal_py_int_get(h_obj));

  if (s == NULL) {
    pm_metal_py_raise_value_error("tar: bad handle");
  }

  return pm_metal_py_int_new((int64_t)pm_metal_util_tar_iter_is_dir(&s->it));
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_tar_is_dir_obj, py_tar_is_dir);
PM_METAL_PY_BIND(
  g_py_bind_tar_is_dir, "pymergetic.metal.tar", "is_dir", py_tar_is_dir_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_tar_read(mp_obj_t h_obj, mp_obj_t n_obj)
{
  tar_py_slot_t    *s = TarSlotGet(pm_metal_py_int_get(h_obj));
  int64_t           n = pm_metal_py_int_get(n_obj);
  uint8_t           stack_buf[512];
  uint8_t          *buf;
  int32_t           got;
  pm_metal_py_obj_t out;

  if (s == NULL || n < 0) {
    pm_metal_py_raise_value_error("tar: bad args");
  }

  buf = stack_buf;
  if ((uint64_t)n > sizeof(stack_buf)) {
    buf = (uint8_t *)pm_metal_mem_alloc((uint32_t)n, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (buf == NULL) {
      pm_metal_py_raise_runtime_error("tar: alloc failed");
    }
  }

  got = pm_metal_util_tar_iter_read(&s->it, buf, (size_t)n);
  if (got < 0) {
    if (buf != stack_buf) {
      pm_metal_mem_free(buf);
    }

    pm_metal_py_raise_value_error("tar: read failed");
  }

  out = pm_metal_py_bytes_new(buf, (size_t)got);
  if (buf != stack_buf) {
    pm_metal_mem_free(buf);
  }

  return out;
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_tar_read_obj, py_tar_read);
PM_METAL_PY_BIND(
  g_py_bind_tar_read, "pymergetic.metal.tar", "read", py_tar_read_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_tar_close_read(mp_obj_t h_obj)
{
  tar_py_slot_t *s = TarSlotGet(pm_metal_py_int_get(h_obj));

  if (s != NULL) {
    pm_metal_util_tar_iter_close(&s->it);
    TarSlotRelease(s);
  }

  return pm_metal_py_obj_none();
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_tar_close_read_obj, py_tar_close_read);
PM_METAL_PY_BIND(g_py_bind_tar_close_read,
                 "pymergetic.metal.tar",
                 "close_read",
                 py_tar_close_read_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t py_tar_open_write(mp_obj_t cap_obj)
{
  int64_t        cap = pm_metal_py_int_get(cap_obj);
  int32_t        idx;
  tar_py_slot_t *s;

  if (cap <= 0) {
    pm_metal_py_raise_value_error("tar: bad cap");
  }

  idx = TarSlotClaim();
  if (idx < 0) {
    pm_metal_py_raise_runtime_error("tar: no free slots");
  }

  s      = &g_tar_py_slots[idx];
  s->buf = (uint8_t *)pm_metal_mem_alloc((uint32_t)cap, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (s->buf == NULL) {
    TarSlotRelease(s);
    pm_metal_py_raise_runtime_error("tar: alloc failed");
  }

  s->cap       = (size_t)cap;
  s->is_writer = 1;
  pm_metal_util_tar_writer_init(&s->wr, s->buf, s->cap);
  return pm_metal_py_int_new((int64_t)idx + 1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_tar_open_write_obj, py_tar_open_write);
PM_METAL_PY_BIND(g_py_bind_tar_open_write,
                 "pymergetic.metal.tar",
                 "open_write",
                 py_tar_open_write_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t py_tar_put(size_t n_args, const mp_obj_t *args)
{
  tar_py_slot_t *s       = TarSlotGet(pm_metal_py_int_get(args[0]));
  const char    *name    = mp_obj_str_get_str(args[1]);
  int32_t        is_dir  = (int32_t)pm_metal_py_int_get(args[2]);
  const uint8_t *src     = NULL;
  size_t         src_len = 0;

  if (s == NULL || !s->is_writer) {
    pm_metal_py_raise_value_error("tar: bad handle");
  }

  if (!pm_metal_py_obj_is_none(args[3])) {
    (void)pm_metal_py_buf_get(args[3], &src, &src_len);
  }

  if (pm_metal_util_tar_writer_put(&s->wr, name, is_dir, src, src_len) != 0) {
    pm_metal_py_raise_value_error("tar: put failed");
  }

  return pm_metal_py_obj_none();
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_tar_put_obj, 4, 4, py_tar_put);
PM_METAL_PY_BIND(
  g_py_bind_tar_put, "pymergetic.metal.tar", "put", py_tar_put_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_tar_finish(mp_obj_t h_obj)
{
  tar_py_slot_t    *s = TarSlotGet(pm_metal_py_int_get(h_obj));
  int64_t           total;
  pm_metal_py_obj_t out;

  if (s == NULL || !s->is_writer) {
    pm_metal_py_raise_value_error("tar: bad handle");
  }

  total = pm_metal_util_tar_writer_finish(&s->wr);
  if (total < 0) {
    TarSlotRelease(s);
    pm_metal_py_raise_value_error("tar: finish failed");
  }

  out = pm_metal_py_bytes_new(s->buf, (size_t)total);
  TarSlotRelease(s);
  return out;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_tar_finish_obj, py_tar_finish);
PM_METAL_PY_BIND(
  g_py_bind_tar_finish, "pymergetic.metal.tar", "finish", py_tar_finish_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_tar_close_write(mp_obj_t h_obj)
{
  tar_py_slot_t *s = TarSlotGet(pm_metal_py_int_get(h_obj));

  TarSlotRelease(s);
  return pm_metal_py_obj_none();
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_tar_close_write_obj, py_tar_close_write);
PM_METAL_PY_BIND(g_py_bind_tar_close_write,
                 "pymergetic.metal.tar",
                 "close_write",
                 py_tar_close_write_obj,
                 PM_METAL_PY_SYNC);
