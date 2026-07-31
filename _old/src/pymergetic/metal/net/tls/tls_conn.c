/** @file
  Persistent TLS/TCP connection slots — pymergetic.metal.tls.*'s backing
  store (tls_py_bind.c). Same connect/handshake/wire-pump shape http.c
  already runs inline; pulled out here as its own small state machine so a
  connection outlives any single Python-level await (connect() completes
  and its coroutine is gone, but the socket + mbedTLS session sit in a
  slot, ready for a later write()/read() coroutine to pick up).
  (impl: efi|bios)
**/
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/net/ip/ip.h>
#include <pymergetic/metal/net/tls/tls.h>
#include <pymergetic/metal/net/tls/tls_conn.h>
#include <pymergetic/metal/runtime/async/async.h>

#include <stddef.h>
#include <stdint.h>

#define TLS_CONN_MAX      4u
#define TLS_CONN_HOST_MAX 128u

typedef struct {
  int32_t                 used;
  pm_metal_net_ip_sock_h  sock;
  pm_metal_net_tls_h      tls_h;
  int32_t                 use_tls;
  pm_metal_net_tls_wire_t wire;
  uint8_t                 rbuf[PM_METAL_TLS_CONN_IO_MAX];
  uint32_t                rbuf_len;
  uint8_t                 wbuf[PM_METAL_TLS_CONN_IO_MAX];
  uint32_t                wbuf_len;
  uint32_t                wbuf_off;
} tls_conn_slot_t;

static tls_conn_slot_t mTlsConnSlots[TLS_CONN_MAX];

typedef enum {
  TLS_CONN_OP_CONNECT = 0,
  TLS_CONN_OP_WRITE,
  TLS_CONN_OP_READ
} tls_conn_op_kind_t;

typedef enum {
  TLS_CONN_ST_INIT = 0,
  TLS_CONN_ST_DNS,
  TLS_CONN_ST_DNS_AW,
  TLS_CONN_ST_SOCK,
  TLS_CONN_ST_CONNECT,
  TLS_CONN_ST_CONNECT_AW,
  TLS_CONN_ST_HANDSHAKE,
  TLS_CONN_ST_WIRE_AW,
  TLS_CONN_ST_IO,
  TLS_CONN_ST_IO_AW,
  TLS_CONN_ST_OK,
  TLS_CONN_ST_FAIL
} tls_conn_op_step_t;

typedef struct {
  tls_conn_op_kind_t      kind;
  tls_conn_op_step_t      step;
  tls_conn_slot_t        *slot;
  pm_metal_async_handle_t aw;
  char                    host[TLS_CONN_HOST_MAX];
  uint16_t                port;
  uint32_t                want;
} tls_conn_op_t;

static tls_conn_slot_t *TlsConnSlotFromHandle(pm_metal_net_tls_conn_h ch)
{
  uint32_t idx;

  if (ch == PM_METAL_TLS_CONN_INVALID) {
    return NULL;
  }

  idx = ch - 1u;
  if (idx >= TLS_CONN_MAX || !mTlsConnSlots[idx].used) {
    return NULL;
  }

  return &mTlsConnSlots[idx];
}

static void TlsConnTeardownSlot(tls_conn_slot_t *s)
{
  if (s->tls_h != PM_METAL_TLS_INVALID) {
    pm_metal_net_tls_close(s->tls_h);
    s->tls_h = PM_METAL_TLS_INVALID;
  }

  if (s->sock != PM_METAL_NET_IP_SOCK_INVALID) {
    pm_metal_net_ip_close(s->sock);
    s->sock = PM_METAL_NET_IP_SOCK_INVALID;
  }
}

pm_metal_net_tls_conn_h pm_metal_net_tls_conn_open(void)
{
  uint32_t i;

  for (i = 0; i < TLS_CONN_MAX; i++) {
    if (!mTlsConnSlots[i].used) {
      memset(&mTlsConnSlots[i], 0, sizeof(mTlsConnSlots[i]));
      mTlsConnSlots[i].used  = 1;
      mTlsConnSlots[i].sock  = PM_METAL_NET_IP_SOCK_INVALID;
      mTlsConnSlots[i].tls_h = PM_METAL_TLS_INVALID;
      return (pm_metal_net_tls_conn_h)(i + 1u);
    }
  }

  return PM_METAL_TLS_CONN_INVALID;
}

void pm_metal_net_tls_conn_close(pm_metal_net_tls_conn_h ch)
{
  tls_conn_slot_t *s = TlsConnSlotFromHandle(ch);

  if (s == NULL) {
    return;
  }

  TlsConnTeardownSlot(s);
  memset(s, 0, sizeof(*s));
}

const uint8_t *pm_metal_net_tls_conn_read_buf(pm_metal_net_tls_conn_h ch)
{
  tls_conn_slot_t *s = TlsConnSlotFromHandle(ch);

  return (s != NULL) ? s->rbuf : NULL;
}

static int32_t TlsConnHostIsLiteral(const char *host)
{
  const char *p;

  for (p = host; *p != '\0'; p++) {
    if (*p == ':') {
      return 1; /* IPv6 literal */
    }

    if ((*p < '0' || *p > '9') && *p != '.') {
      return 0; /* any letter -> a real hostname */
    }
  }

  return 1; /* all digits/dots -> IPv4 literal */
}

static tls_conn_op_t *TlsConnOpFromHandle(pm_metal_async_handle_t hnd)
{
  return (tls_conn_op_t *)(uintptr_t)pm_metal_async_coro_state(hnd);
}

static pm_metal_status_t TlsConnOpStep(pm_metal_async_handle_t self_h)
{
  tls_conn_op_t   *op = TlsConnOpFromHandle(self_h);
  tls_conn_slot_t *s;
  int32_t          he;
  uint32_t         n;

  if (op == NULL) {
    return PM_METAL_ERROR;
  }

  s = op->slot;

  switch (op->step) {
  case TLS_CONN_ST_INIT:
    if (op->kind == TLS_CONN_OP_CONNECT) {
      TlsConnTeardownSlot(s);
      s->wire.len = 0;
      s->wire.off = 0;
      op->step    = TlsConnHostIsLiteral(op->host) ? TLS_CONN_ST_SOCK : TLS_CONN_ST_DNS;
    } else {
      op->step = TLS_CONN_ST_IO;
    }

    return PM_METAL_PENDING;

  case TLS_CONN_ST_DNS:
    op->aw = pm_metal_net_ip_dns(op->host);
    if (op->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      op->step = TLS_CONN_ST_FAIL;
      return PM_METAL_PENDING;
    }

    op->step = TLS_CONN_ST_DNS_AW;
    return pm_metal_async_await(self_h, op->aw);

  case TLS_CONN_ST_DNS_AW:
    if (pm_metal_async_result_u32(self_h) == 0) {
      op->step = TLS_CONN_ST_FAIL;
      return PM_METAL_PENDING;
    }

    op->step = TLS_CONN_ST_SOCK;
    return PM_METAL_PENDING;

  case TLS_CONN_ST_SOCK: {
    uint32_t domain =
      (strstr(op->host, ":") != NULL) ? PM_METAL_NET_IP_AF_INET6 : PM_METAL_NET_IP_AF_INET;

    s->sock = pm_metal_net_ip_socket(domain, PM_METAL_NET_IP_SOCK_STREAM);
    if (s->sock == PM_METAL_NET_IP_SOCK_INVALID) {
      op->step = TLS_CONN_ST_FAIL;
      return PM_METAL_PENDING;
    }

    op->step = TLS_CONN_ST_CONNECT;
    return PM_METAL_PENDING;
  }

  case TLS_CONN_ST_CONNECT:
    op->aw = pm_metal_net_ip_connect(s->sock, op->host, op->port);
    if (op->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      op->step = TLS_CONN_ST_FAIL;
      return PM_METAL_PENDING;
    }

    op->step = TLS_CONN_ST_CONNECT_AW;
    return pm_metal_async_await(self_h, op->aw);

  case TLS_CONN_ST_CONNECT_AW:
    if (pm_metal_async_result_u32(self_h) == 0) {
      op->step = TLS_CONN_ST_FAIL;
      return PM_METAL_PENDING;
    }

    if (!s->use_tls) {
      op->step = TLS_CONN_ST_OK;
      return PM_METAL_PENDING;
    }

    s->tls_h = pm_metal_net_tls_open(op->host);
    if (s->tls_h == PM_METAL_TLS_INVALID ||
        pm_metal_net_tls_bind(s->tls_h, s->sock, &s->wire) != 0) {
      op->step = TLS_CONN_ST_FAIL;
      return PM_METAL_PENDING;
    }

    op->step = TLS_CONN_ST_HANDSHAKE;
    return PM_METAL_PENDING;

  case TLS_CONN_ST_HANDSHAKE:
    he = pm_metal_net_tls_handshake_step(s->tls_h);
    if (he == 0) {
      op->step = TLS_CONN_ST_OK;
      return PM_METAL_PENDING;
    }

    if (he < 0) {
      op->step = TLS_CONN_ST_FAIL;
      return PM_METAL_PENDING;
    }

    /* WANT_READ or WANT_WRITE: same wire-pump either way, matching http.c's
     * own HTTP_STEP_TLS (the send side of a handshake is a direct blocking
     * pm_metal_net_ip_send from tls.c's bio callback, never awaited here). */
    op->aw = pm_metal_net_ip_recv(s->sock, s->wire.buf, sizeof(s->wire.buf));
    if (op->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      op->step = TLS_CONN_ST_FAIL;
      return PM_METAL_PENDING;
    }

    op->step = TLS_CONN_ST_WIRE_AW;
    return pm_metal_async_await(self_h, op->aw);

  case TLS_CONN_ST_WIRE_AW:
    n = pm_metal_async_result_u32(self_h);
    if (n == 0) {
      op->step = TLS_CONN_ST_FAIL;
      return PM_METAL_PENDING;
    }

    s->wire.len = n;
    s->wire.off = 0;
    op->step    = (op->kind == TLS_CONN_OP_CONNECT) ? TLS_CONN_ST_HANDSHAKE : TLS_CONN_ST_IO;
    return PM_METAL_PENDING;

  case TLS_CONN_ST_IO:
    if (op->kind == TLS_CONN_OP_WRITE) {
      if (s->wbuf_off >= s->wbuf_len) {
        op->step = TLS_CONN_ST_OK;
        return PM_METAL_PENDING;
      }

      if (s->use_tls) {
        int32_t e =
          pm_metal_net_tls_write(s->tls_h, s->wbuf + s->wbuf_off, s->wbuf_len - s->wbuf_off);

        if (e > 0) {
          s->wbuf_off += (uint32_t)e;
          op->step = (s->wbuf_off >= s->wbuf_len) ? TLS_CONN_ST_OK : TLS_CONN_ST_IO;
          return PM_METAL_PENDING;
        }

        if (e == PM_METAL_TLS_WANT_READ) {
          op->aw = pm_metal_net_ip_recv(s->sock, s->wire.buf, sizeof(s->wire.buf));
          if (op->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
            op->step = TLS_CONN_ST_FAIL;
            return PM_METAL_PENDING;
          }

          op->step = TLS_CONN_ST_WIRE_AW;
          return pm_metal_async_await(self_h, op->aw);
        }

        if (e == PM_METAL_TLS_WANT_WRITE) {
          return pm_metal_async_await(self_h, pm_metal_async_sleep_us(2000));
        }

        op->step = TLS_CONN_ST_FAIL;
        return PM_METAL_PENDING;
      }

      {
        uint32_t nsend =
          pm_metal_net_ip_send(s->sock, s->wbuf + s->wbuf_off, s->wbuf_len - s->wbuf_off);

        if (nsend > 0) {
          s->wbuf_off += nsend;
          op->step = (s->wbuf_off >= s->wbuf_len) ? TLS_CONN_ST_OK : TLS_CONN_ST_IO;
          return PM_METAL_PENDING;
        }

        return pm_metal_async_await(self_h, pm_metal_async_sleep_us(2000));
      }
    }

    /* TLS_CONN_OP_READ */
    if (s->use_tls) {
      int32_t e = pm_metal_net_tls_read(s->tls_h, s->rbuf, op->want);

      if (e > 0) {
        s->rbuf_len = (uint32_t)e;
        op->step    = TLS_CONN_ST_OK;
        return PM_METAL_PENDING;
      }

      if (e == 0) {
        s->rbuf_len = 0;
        op->step    = TLS_CONN_ST_OK;
        return PM_METAL_PENDING;
      }

      if (e == PM_METAL_TLS_WANT_READ || e == PM_METAL_TLS_WANT_WRITE) {
        op->aw = pm_metal_net_ip_recv(s->sock, s->wire.buf, sizeof(s->wire.buf));
        if (op->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
          op->step = TLS_CONN_ST_FAIL;
          return PM_METAL_PENDING;
        }

        op->step = TLS_CONN_ST_WIRE_AW;
        return pm_metal_async_await(self_h, op->aw);
      }

      op->step = TLS_CONN_ST_FAIL;
      return PM_METAL_PENDING;
    }

    op->aw = pm_metal_net_ip_recv(s->sock, s->rbuf, op->want);
    if (op->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      op->step = TLS_CONN_ST_FAIL;
      return PM_METAL_PENDING;
    }

    op->step = TLS_CONN_ST_IO_AW;
    return pm_metal_async_await(self_h, op->aw);

  case TLS_CONN_ST_IO_AW:
    s->rbuf_len = pm_metal_async_result_u32(self_h);
    op->step    = TLS_CONN_ST_OK;
    return PM_METAL_PENDING;

  case TLS_CONN_ST_OK:
    if (op->kind == TLS_CONN_OP_CONNECT) {
      pm_metal_async_set_result_u32(self_h, 1u);
    } else if (op->kind == TLS_CONN_OP_WRITE) {
      pm_metal_async_set_result_u32(self_h, s->wbuf_off);
    } else {
      pm_metal_async_set_result_u32(self_h, s->rbuf_len);
    }

    return PM_METAL_DONE;

  case TLS_CONN_ST_FAIL:
    if (op->kind == TLS_CONN_OP_CONNECT) {
      TlsConnTeardownSlot(s);
    }

    pm_metal_async_set_result_u32(self_h, 0u);
    return PM_METAL_DONE;

  default:
    return PM_METAL_ERROR;
  }
}

static pm_metal_async_handle_t TlsConnOpStart(tls_conn_slot_t *s, tls_conn_op_kind_t kind)
{
  pm_metal_async_handle_t ah;
  tls_conn_op_t          *op;

  ah = pm_metal_async_coro_create(TlsConnOpStep, sizeof(*op));
  if (ah == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  op = (tls_conn_op_t *)(uintptr_t)pm_metal_async_coro_state(ah);
  if (op == NULL) {
    pm_metal_async_coro_close(ah);
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  memset(op, 0, sizeof(*op));
  op->kind = kind;
  op->step = TLS_CONN_ST_INIT;
  op->slot = s;
  return ah;
}

pm_metal_async_handle_t pm_metal_net_tls_conn_connect(pm_metal_net_tls_conn_h ch,
                                                      const char             *host,
                                                      uint16_t                port,
                                                      int32_t                 use_tls)
{
  tls_conn_slot_t        *s = TlsConnSlotFromHandle(ch);
  pm_metal_async_handle_t ah;
  tls_conn_op_t          *op;

  if (s == NULL || host == NULL || host[0] == '\0') {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  ah = TlsConnOpStart(s, TLS_CONN_OP_CONNECT);
  if (ah == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  op = TlsConnOpFromHandle(ah);
  snprintf(op->host, sizeof(op->host), "%s", host);
  op->port   = port;
  s->use_tls = use_tls ? 1 : 0;
  return ah;
}

pm_metal_async_handle_t pm_metal_net_tls_conn_write(pm_metal_net_tls_conn_h ch,
                                                    const void             *data,
                                                    uint32_t                len)
{
  tls_conn_slot_t        *s = TlsConnSlotFromHandle(ch);
  pm_metal_async_handle_t ah;

  if (s == NULL || data == NULL || len == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (len > PM_METAL_TLS_CONN_IO_MAX) {
    len = PM_METAL_TLS_CONN_IO_MAX;
  }

  memcpy(s->wbuf, data, len);
  s->wbuf_len = len;
  s->wbuf_off = 0;

  ah = TlsConnOpStart(s, TLS_CONN_OP_WRITE);
  return ah;
}

pm_metal_async_handle_t pm_metal_net_tls_conn_read(pm_metal_net_tls_conn_h ch, uint32_t want)
{
  tls_conn_slot_t        *s = TlsConnSlotFromHandle(ch);
  tls_conn_op_t          *op;
  pm_metal_async_handle_t ah;

  if (s == NULL || want == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (want > PM_METAL_TLS_CONN_IO_MAX) {
    want = PM_METAL_TLS_CONN_IO_MAX;
  }

  s->rbuf_len = 0;
  ah          = TlsConnOpStart(s, TLS_CONN_OP_READ);
  if (ah == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  op       = TlsConnOpFromHandle(ah);
  op->want = want;
  return ah;
}
