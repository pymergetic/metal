/** @file
  pymergetic.metal.net.http — thin async HTTP/HTTPS GET facade for Python,
  wired as a nested submodule of pymergetic.metal.net (see net_py_bind.c
  and py_bind.c's dotted-path resolver — no extra plumbing needed for the
  nesting). Mirrors dns_last_ntoa's "await the op, then a separate sync
  call reads the last result" idiom: pm_metal_net_http_get's dest buffer
  is one persistent lazily-allocated scratch buffer — one GET in flight
  at a time is the intended usage shape for a REPL-driven facade, not a
  connection pool.
**/
#include <string.h>

#include <pymergetic/metal/net/http/http.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>
#include <pymergetic/metal/runtime/mem/mem.h>

/* Only MicroPython header this file needs — see fs_py_bind.c's comment. */
#include "py/obj.h"

#define NET_HTTP_PY_GET_CAP (64u * 1024u)

static uint8_t *g_http_get_buf;

typedef struct {
  pm_metal_async_handle_t ah;
} net_http_py_ctx_t;

static void HttpGetBytesFn(void *ctx, const uint8_t **out_ptr, size_t *out_len)
{
  net_http_py_ctx_t *c = (net_http_py_ctx_t *)ctx;

  *out_ptr = g_http_get_buf;
  *out_len = (size_t)pm_metal_net_http_body_len(c->ah);
  pm_metal_mem_free(c);
}

/** await get(url) -> bytes (the response body; empty bytes on transport
 * failure — check last_status() for why). Body truncated to
 * NET_HTTP_PY_GET_CAP if the server sends more. */
static mp_obj_t py_net_http_get(mp_obj_t url_obj)
{
  pm_metal_async_handle_t ah;
  net_http_py_ctx_t      *ctx;

  if (g_http_get_buf == NULL) {
    g_http_get_buf =
      (uint8_t *)pm_metal_mem_alloc(NET_HTTP_PY_GET_CAP, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (g_http_get_buf == NULL) {
      pm_metal_py_raise_runtime_error("net.http: alloc failed");
    }
  }

  ah = pm_metal_net_http_get(mp_obj_str_get_str(url_obj), g_http_get_buf, NET_HTTP_PY_GET_CAP);
  if (ah == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_py_raise_value_error("net.http: get failed to start");
  }

  ctx = (net_http_py_ctx_t *)pm_metal_mem_alloc(
    (uint32_t)sizeof(*ctx), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (ctx == NULL) {
    pm_metal_py_raise_runtime_error("net.http: alloc failed");
  }

  ctx->ah = ah;
  return pm_metal_py_new_awaitable_bytes(ah, HttpGetBytesFn, ctx);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_net_http_get_obj, py_net_http_get);
PM_METAL_PY_BIND(g_py_bind_net_http_get,
                 "pymergetic.metal.net.http",
                 "get",
                 py_net_http_get_obj,
                 PM_METAL_PY_SYNC);

/** last_status() -> int HTTP status (200, …) from the most recently
 * completed await get(...); 0 on transport failure. */
static mp_obj_t py_net_http_last_status(void)
{
  return pm_metal_py_int_new((int64_t)pm_metal_net_http_status(PM_METAL_ASYNC_HANDLE_INVALID));
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_net_http_last_status_obj, py_net_http_last_status);
PM_METAL_PY_BIND(g_py_bind_net_http_last_status,
                 "pymergetic.metal.net.http",
                 "last_status",
                 py_net_http_last_status_obj,
                 PM_METAL_PY_SYNC);
