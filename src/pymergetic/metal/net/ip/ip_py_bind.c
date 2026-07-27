/** @file
  pymergetic.metal.net — thin async TCP/UDP socket facade for Python
  (honest primitive, same spirit as pymergetic.metal.tls — see
  docs/MICROPYTHON.md: no CPython socket/select-shaped shim, no blocking
  calls). Wraps dev/net/net.h directly; every op here is either a plain
  sync call or an await-bridge over net.h's own async handle — no local
  state machine needed (contrast tls_conn.c, which needs one because a
  handshake is multiple net.h ops chained together; net.h's own
  connect/listen/accept/recv/dns are each already a single async op).

  domain/type are net.h's raw PM_METAL_NET_AF_* / PM_METAL_NET_SOCK_* ints
  (1/2 each — no named constants exposed yet, see socket()'s own comment);
  socket() defaults to AF_INET+SOCK_STREAM when omitted, the common case.
**/
#include <string.h>

#include <pymergetic/metal/dev/net/net.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>
#include <pymergetic/metal/runtime/mem/mem.h>

/* Only MicroPython header this file needs — see fs_py_bind.c's comment;
 * mp_obj_str_get_str (host/ifname string args) lives here too. */
#include "py/obj.h"

/*
 * pm_metal_net_recv(h, ptr, len) writes into `ptr` for the *entire* async
 * operation's lifetime (every poll until done), not just at completion —
 * unlike tls_conn_read, which hands back a pointer into its own
 * long-lived per-connection buffer that outlives any single read(). A
 * fresh pm_metal_mem_alloc'd-per-call buffer freed inside the
 * pm_metal_py_new_awaitable_bytes resolve callback would be a
 * use-after-free (the actual bytes copy happens right after that callback
 * returns, still reading the pointer it set) — so recv() keeps one
 * persistent scratch buffer per socket handle here instead, sized like
 * tls_conn's own PM_METAL_TLS_CONN_IO_MAX (4096): allocated lazily on
 * first recv() for a handle, freed on close(). Sized for a handful of
 * concurrent sockets (isolated Python contexts included) — not a hard
 * cap on how many sockets exist, only on how many have an outstanding
 * recv() scratch buffer at once.
 */
#define NET_PY_RECV_SLOTS 8u
#define NET_PY_RECV_CAP   4096u

typedef struct {
  pm_metal_net_sock_h sock;
  uint8_t             buf[NET_PY_RECV_CAP];
} net_py_recv_slot_t;

static net_py_recv_slot_t mRecvSlots[NET_PY_RECV_SLOTS];

static net_py_recv_slot_t *NetPyRecvSlotFor(pm_metal_net_sock_h h)
{
  uint32_t i;
  int32_t  free_idx = -1;

  for (i = 0; i < NET_PY_RECV_SLOTS; i++) {
    if (mRecvSlots[i].sock == h) {
      return &mRecvSlots[i];
    }

    if (mRecvSlots[i].sock == PM_METAL_NET_SOCK_INVALID && free_idx < 0) {
      free_idx = (int32_t)i;
    }
  }

  if (free_idx < 0) {
    return NULL;
  }

  mRecvSlots[free_idx].sock = h;
  return &mRecvSlots[free_idx];
}

static void NetPyRecvSlotFree(pm_metal_net_sock_h h)
{
  uint32_t i;

  for (i = 0; i < NET_PY_RECV_SLOTS; i++) {
    if (mRecvSlots[i].sock == h) {
      mRecvSlots[i].sock = PM_METAL_NET_SOCK_INVALID;
      return;
    }
  }
}

typedef struct {
  pm_metal_async_handle_t ah;
  net_py_recv_slot_t     *slot;
} net_py_recv_ctx_t;

static void NetRecvBytesFn(void *ctx, const uint8_t **out_ptr, size_t *out_len)
{
  net_py_recv_ctx_t *c = (net_py_recv_ctx_t *)ctx;

  *out_ptr = c->slot->buf;
  *out_len = (size_t)pm_metal_async_result_u32(c->ah);
  pm_metal_mem_free(c);
}

/** socket([domain[, type]]) -> int handle. Omitted -> AF_INET+SOCK_STREAM
 * (the common case) — see file header for why there's no named-constant
 * form yet. */
static mp_obj_t py_net_socket(size_t n_args, const mp_obj_t *args)
{
  uint32_t domain = (n_args >= 1) ? (uint32_t)pm_metal_py_int_get(args[0]) : PM_METAL_NET_AF_INET;
  uint32_t type = (n_args >= 2) ? (uint32_t)pm_metal_py_int_get(args[1]) : PM_METAL_NET_SOCK_STREAM;

  return pm_metal_py_int_new((int64_t)pm_metal_net_socket(domain, type));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_net_socket_obj, 0, 2, py_net_socket);
PM_METAL_PY_BIND(
  g_py_bind_net_socket, "pymergetic.metal.net", "socket", py_net_socket_obj, PM_METAL_PY_SYNC);

/** bind_if(h[, ifname]) -> int 0/-1. ifname None/omitted -> default route. */
static mp_obj_t py_net_bind_if(size_t n_args, const mp_obj_t *args)
{
  pm_metal_net_sock_h h = (pm_metal_net_sock_h)pm_metal_py_int_get(args[0]);
  const char         *ifname;

  ifname = (n_args >= 2 && args[1] != mp_const_none) ? mp_obj_str_get_str(args[1]) : NULL;
  return pm_metal_py_int_new(pm_metal_net_bind_if(h, ifname));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_net_bind_if_obj, 1, 2, py_net_bind_if);
PM_METAL_PY_BIND(
  g_py_bind_net_bind_if, "pymergetic.metal.net", "bind_if", py_net_bind_if_obj, PM_METAL_PY_SYNC);

/** send(h, data) -> int bytes sent (0 if no space / no remote yet — sync
 * façade, never awaits; see net.h's own doc comment). */
static mp_obj_t py_net_send(mp_obj_t h_obj, mp_obj_t data_obj)
{
  pm_metal_net_sock_h h = (pm_metal_net_sock_h)pm_metal_py_int_get(h_obj);
  const uint8_t      *buf;
  size_t              len;

  (void)pm_metal_py_buf_get(data_obj, &buf, &len);
  return pm_metal_py_int_new((int64_t)pm_metal_net_send(h, buf, (uint32_t)len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_net_send_obj, py_net_send);
PM_METAL_PY_BIND(
  g_py_bind_net_send, "pymergetic.metal.net", "send", py_net_send_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_net_close(mp_obj_t h_obj)
{
  pm_metal_net_sock_h h = (pm_metal_net_sock_h)pm_metal_py_int_get(h_obj);

  pm_metal_net_close(h);
  NetPyRecvSlotFree(h);
  return pm_metal_py_obj_none();
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_net_close_obj, py_net_close);
PM_METAL_PY_BIND(
  g_py_bind_net_close, "pymergetic.metal.net", "close", py_net_close_obj, PM_METAL_PY_SYNC);

/** await connect(h, host, port) -> u32 1 ok / 0 fail. DNS happens inside
 * net.h's own coroutine when host isn't a literal. */
static mp_obj_t py_net_connect(mp_obj_t h_obj, mp_obj_t host_obj, mp_obj_t port_obj)
{
  pm_metal_async_handle_t ah = pm_metal_net_connect((pm_metal_net_sock_h)pm_metal_py_int_get(h_obj),
                                                    mp_obj_str_get_str(host_obj),
                                                    (uint32_t)pm_metal_py_int_get(port_obj));

  if (ah == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_py_raise_value_error("net: connect failed to start");
  }

  return pm_metal_py_new_awaitable_u32(ah);
}
static MP_DEFINE_CONST_FUN_OBJ_3(py_net_connect_obj, py_net_connect);
PM_METAL_PY_BIND(
  g_py_bind_net_connect, "pymergetic.metal.net", "connect", py_net_connect_obj, PM_METAL_PY_SYNC);

/** await listen(h, port) -> u32 1 ok / 0 fail. TCP only (net.h/net_lwip.c). */
static mp_obj_t py_net_listen(mp_obj_t h_obj, mp_obj_t port_obj)
{
  pm_metal_async_handle_t ah = pm_metal_net_listen((pm_metal_net_sock_h)pm_metal_py_int_get(h_obj),
                                                   (uint32_t)pm_metal_py_int_get(port_obj));

  if (ah == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_py_raise_value_error("net: listen failed to start");
  }

  return pm_metal_py_new_awaitable_u32(ah);
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_net_listen_obj, py_net_listen);
PM_METAL_PY_BIND(
  g_py_bind_net_listen, "pymergetic.metal.net", "listen", py_net_listen_obj, PM_METAL_PY_SYNC);

/** await accept(h) -> u32 new socket handle (0 on failure). */
static mp_obj_t py_net_accept(mp_obj_t h_obj)
{
  pm_metal_async_handle_t ah = pm_metal_net_accept((pm_metal_net_sock_h)pm_metal_py_int_get(h_obj));

  if (ah == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_py_raise_value_error("net: accept failed to start");
  }

  return pm_metal_py_new_awaitable_u32(ah);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_net_accept_obj, py_net_accept);
PM_METAL_PY_BIND(
  g_py_bind_net_accept, "pymergetic.metal.net", "accept", py_net_accept_obj, PM_METAL_PY_SYNC);

/** await recv(h, n) -> bytes (n capped to NET_PY_RECV_CAP; empty bytes on
 * clean EOF/error — see net.h/net_lwip.c). */
static mp_obj_t py_net_recv(mp_obj_t h_obj, mp_obj_t n_obj)
{
  pm_metal_net_sock_h     h    = (pm_metal_net_sock_h)pm_metal_py_int_get(h_obj);
  uint32_t                want = (uint32_t)pm_metal_py_int_get(n_obj);
  net_py_recv_slot_t     *slot;
  pm_metal_async_handle_t ah;
  net_py_recv_ctx_t      *ctx;

  slot = NetPyRecvSlotFor(h);
  if (slot == NULL) {
    pm_metal_py_raise_runtime_error("net: recv: too many concurrent sockets");
  }

  if (want > NET_PY_RECV_CAP) {
    want = NET_PY_RECV_CAP;
  }

  ah = pm_metal_net_recv(h, slot->buf, want);
  if (ah == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_py_raise_value_error("net: recv failed to start");
  }

  ctx = (net_py_recv_ctx_t *)pm_metal_mem_alloc(
    (uint32_t)sizeof(*ctx), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (ctx == NULL) {
    pm_metal_py_raise_runtime_error("net: alloc failed");
  }

  ctx->ah   = ah;
  ctx->slot = slot;
  return pm_metal_py_new_awaitable_bytes(ah, NetRecvBytesFn, ctx);
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_net_recv_obj, py_net_recv);
PM_METAL_PY_BIND(
  g_py_bind_net_recv, "pymergetic.metal.net", "recv", py_net_recv_obj, PM_METAL_PY_SYNC);

/** await dns(host) -> u32 1 ok / 0 fail. On 1: dns_last_ntoa() has the
 * resolved address (same "last completed op" idiom net.h already uses). */
static mp_obj_t py_net_dns(mp_obj_t host_obj)
{
  pm_metal_async_handle_t ah = pm_metal_net_dns(mp_obj_str_get_str(host_obj));

  if (ah == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_py_raise_value_error("net: dns failed to start");
  }

  return pm_metal_py_new_awaitable_u32(ah);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_net_dns_obj, py_net_dns);
PM_METAL_PY_BIND(
  g_py_bind_net_dns, "pymergetic.metal.net", "dns", py_net_dns_obj, PM_METAL_PY_SYNC);

/** dns_last_ntoa() -> str address, or None. Valid only right after a
 * successful await dns(...). */
static mp_obj_t py_net_dns_last_ntoa(void)
{
  char addr[64];

  if (pm_metal_net_dns_last_ntoa(addr, sizeof(addr)) != 0) {
    return pm_metal_py_obj_none();
  }

  return pm_metal_py_str_new(addr);
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_net_dns_last_ntoa_obj, py_net_dns_last_ntoa);
PM_METAL_PY_BIND(g_py_bind_net_dns_last_ntoa,
                 "pymergetic.metal.net",
                 "dns_last_ntoa",
                 py_net_dns_last_ntoa_obj,
                 PM_METAL_PY_SYNC);
