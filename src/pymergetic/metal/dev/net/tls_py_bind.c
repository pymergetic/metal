/** @file
  pymergetic.metal.tls — low-level async TCP+TLS connections for Python
  (backs mods/py/stdlib_src/... nothing yet: per docs/MICROPYTHON.md this
  is deliberately not a CPython ssl-module shim, just the real primitive a
  future pure-Python wrapper would sit on top of). Wraps
  dev/net/tls_conn.h's own connection-slot state machine — one open()'d
  handle survives across separate connect()/write()/read() awaits, same
  socket + mbedTLS session the whole time.
**/
#include <pymergetic/metal/dev/net/tls_conn.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>
#include <pymergetic/metal/runtime/mem/mem.h>

#include "py/obj.h"

typedef struct {
  pm_metal_tls_conn_h     ch;
  pm_metal_async_handle_t ah;
} tls_read_ctx_t;

static void TlsReadBytesFn(void *ctx, const uint8_t **out_ptr, size_t *out_len)
{
  tls_read_ctx_t *c = (tls_read_ctx_t *)ctx;

  *out_ptr = pm_metal_tls_conn_read_buf(c->ch);
  *out_len = (size_t)pm_metal_async_result_u32(c->ah);
  pm_metal_mem_free(c);
}

static mp_obj_t py_tls_open(void)
{
  return pm_metal_py_int_new((int64_t)pm_metal_tls_conn_open());
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_tls_open_obj, py_tls_open);
PM_METAL_PY_BIND(
  g_py_bind_tls_open, "pymergetic.metal.tls", "open", py_tls_open_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_tls_connect(size_t n_args, const mp_obj_t *args)
{
  pm_metal_tls_conn_h     ch      = (pm_metal_tls_conn_h)pm_metal_py_int_get(args[0]);
  const char             *host    = mp_obj_str_get_str(args[1]);
  uint16_t                port    = (uint16_t)pm_metal_py_int_get(args[2]);
  int32_t                 use_tls = (int32_t)pm_metal_py_int_get(args[3]);
  pm_metal_async_handle_t ah;

  ah = pm_metal_tls_conn_connect(ch, host, port, use_tls);
  if (ah == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_py_raise_value_error("tls: connect failed to start");
  }

  return pm_metal_py_new_awaitable_u32(ah);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_tls_connect_obj, 4, 4, py_tls_connect);
PM_METAL_PY_BIND(
  g_py_bind_tls_connect, "pymergetic.metal.tls", "connect", py_tls_connect_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_tls_write(mp_obj_t h_obj, mp_obj_t data_obj)
{
  pm_metal_tls_conn_h     ch = (pm_metal_tls_conn_h)pm_metal_py_int_get(h_obj);
  const uint8_t          *data;
  size_t                  len;
  pm_metal_async_handle_t ah;

  (void)pm_metal_py_buf_get(data_obj, &data, &len);
  ah = pm_metal_tls_conn_write(ch, data, (uint32_t)len);
  if (ah == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_py_raise_value_error("tls: write failed to start");
  }

  return pm_metal_py_new_awaitable_u32(ah);
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_tls_write_obj, py_tls_write);
PM_METAL_PY_BIND(
  g_py_bind_tls_write, "pymergetic.metal.tls", "write", py_tls_write_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_tls_read(mp_obj_t h_obj, mp_obj_t n_obj)
{
  pm_metal_tls_conn_h     ch   = (pm_metal_tls_conn_h)pm_metal_py_int_get(h_obj);
  uint32_t                want = (uint32_t)pm_metal_py_int_get(n_obj);
  pm_metal_async_handle_t ah;
  tls_read_ctx_t         *ctx;

  ah = pm_metal_tls_conn_read(ch, want);
  if (ah == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_py_raise_value_error("tls: read failed to start");
  }

  ctx = (tls_read_ctx_t *)pm_metal_mem_alloc(
    (uint32_t)sizeof(*ctx), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (ctx == NULL) {
    pm_metal_py_raise_runtime_error("tls: alloc failed");
  }

  ctx->ch = ch;
  ctx->ah = ah;
  return pm_metal_py_new_awaitable_bytes(ah, TlsReadBytesFn, ctx);
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_tls_read_obj, py_tls_read);
PM_METAL_PY_BIND(
  g_py_bind_tls_read, "pymergetic.metal.tls", "read", py_tls_read_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_tls_close(mp_obj_t h_obj)
{
  pm_metal_tls_conn_h ch = (pm_metal_tls_conn_h)pm_metal_py_int_get(h_obj);

  pm_metal_tls_conn_close(ch);
  return pm_metal_py_obj_none();
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_tls_close_obj, py_tls_close);
PM_METAL_PY_BIND(
  g_py_bind_tls_close, "pymergetic.metal.tls", "close", py_tls_close_obj, PM_METAL_PY_SYNC);
