/** @file
  TFTP RRQ client over lwIP UDP (async host coro + guest imports).
**/
#include <pymergetic/metal/dev/net/tftp.h>
#include <pymergetic/metal/dev/net/net.h>
#include <pymergetic/metal/dev/net/net_cfg.h>
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/util/ip.h>
#include <runtime/coro/coro.h>
#include <runtime/time/time.h>

#include "lwipopts.h" /* IWYU pragma: keep */
#include <lwip/udp.h>
#include <lwip/pbuf.h>
#include <lwip/ip_addr.h>
#include <lwip/dns.h>
#include <lwip/inet.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>

#include "wasm_export.h"

#define TFTP_PORT       69u
#define TFTP_BLOCK      512u
#define TFTP_HDR        4u
#define TFTP_PKT_MAX    (TFTP_HDR + TFTP_BLOCK)
#define TFTP_PATH_MAX   192u
#define TFTP_HOST_MAX   128u
#define TFTP_TIMEOUT_US 3000000ull
#define TFTP_RETRIES    5u

#define TFTP_OP_RRQ   1u
#define TFTP_OP_DATA  3u
#define TFTP_OP_ACK   4u
#define TFTP_OP_ERROR 5u

typedef enum {
  TFTP_STEP_RESOLVE = 0,
  TFTP_STEP_DNS_AW,
  TFTP_STEP_OPEN,
  TFTP_STEP_SEND,
  TFTP_STEP_WAIT,
  TFTP_STEP_DONE
} tftp_step_t;

typedef struct {
  pm_metal_coro_t          coro;
  tftp_step_t              step;
  pm_metal_async_handle_t  aw;
  CHAR8                    host[TFTP_HOST_MAX];
  CHAR8                    path[TFTP_PATH_MAX];
  VOID                    *dest;
  UINT32                   dest_cap;
  UINT32                   got;
  UINT32                   status;
  ip_addr_t                server;
  UINT16                   server_port;
  struct udp_pcb          *pcb;
  UINT16                   expect_block;
  UINT32                   retries;
  UINT64                   deadline;
  INT32                    have_pkt;
  UINT8                    rx[TFTP_PKT_MAX];
  UINT16                   rx_len;
  INT32                    last_block;
} tftp_get_t;

STATIC wasm_module_inst_t  mTftpInst;

STATIC struct {
  INT32   valid;
  UINT32  status;
  UINT32  len;
} mTftpLastDone;

STATIC
VOID
TftpTeardown (
  tftp_get_t  *t
  )
{
  if (t == NULL) {
    return;
  }

  if (t->pcb != NULL) {
    udp_remove (t->pcb);
    t->pcb = NULL;
  }
}

STATIC
VOID
TftpRecvCb (
  VOID            *arg,
  struct udp_pcb  *pcb,
  struct pbuf     *p,
  CONST ip_addr_t *addr,
  UINT16           port
  )
{
  tftp_get_t  *t;
  UINT16       n;

  (VOID)pcb;
  t = (tftp_get_t *)arg;
  if (t == NULL || p == NULL) {
    if (p != NULL) {
      pbuf_free (p);
    }

    return;
  }

  if (t->have_pkt) {
    pbuf_free (p);
    return;
  }

  if (addr != NULL) {
    t->server      = *addr;
    t->server_port = port;
  }

  n = (UINT16)(p->tot_len < sizeof (t->rx) ? p->tot_len : sizeof (t->rx));
  pbuf_copy_partial (p, t->rx, n, 0);
  t->rx_len   = n;
  t->have_pkt = 1;
  pbuf_free (p);
}

STATIC
INT32
TftpSendRrq (
  tftp_get_t  *t
  )
{
  UINT8         pkt[TFTP_PKT_MAX];
  UINT16        off;
  UINTN         plen;
  UINTN         mlen;
  struct pbuf  *p;
  err_t         e;

  if (t == NULL || t->pcb == NULL) {
    return -1;
  }

  pkt[0] = 0;
  pkt[1] = (UINT8)TFTP_OP_RRQ;
  off    = 2;
  plen   = AsciiStrLen (t->path);
  if (plen == 0 || off + plen + 1u + 6u >= sizeof (pkt)) {
    return -1;
  }

  CopyMem (pkt + off, t->path, plen);
  off           = (UINT16)(off + plen);
  pkt[off++]    = 0;
  mlen          = 5; /* "octet" */
  CopyMem (pkt + off, "octet", mlen);
  off           = (UINT16)(off + mlen);
  pkt[off++]    = 0;

  p = pbuf_alloc (PBUF_TRANSPORT, off, PBUF_RAM);
  if (p == NULL) {
    return -1;
  }

  CopyMem (p->payload, pkt, off);
  e = udp_sendto (t->pcb, p, &t->server, TFTP_PORT);
  pbuf_free (p);
  return (e == ERR_OK) ? 0 : -1;
}

STATIC
INT32
TftpSendAck (
  tftp_get_t  *t,
  UINT16       block
  )
{
  UINT8         pkt[4];
  struct pbuf  *p;
  err_t         e;

  if (t == NULL || t->pcb == NULL) {
    return -1;
  }

  pkt[0] = 0;
  pkt[1] = (UINT8)TFTP_OP_ACK;
  pkt[2] = (UINT8)(block >> 8);
  pkt[3] = (UINT8)(block & 0xffu);

  p = pbuf_alloc (PBUF_TRANSPORT, 4, PBUF_RAM);
  if (p == NULL) {
    return -1;
  }

  CopyMem (p->payload, pkt, 4);
  e = udp_sendto (t->pcb, p, &t->server, t->server_port);
  pbuf_free (p);
  return (e == ERR_OK) ? 0 : -1;
}

STATIC
INT32
TftpHandlePkt (
  tftp_get_t  *t
  )
{
  UINT16  op;
  UINT16  block;
  UINT16  data_len;
  UINT8  *dst;

  if (t == NULL || t->rx_len < TFTP_HDR) {
    return -1;
  }

  op = (UINT16)(((UINT16)t->rx[0] << 8) | t->rx[1]);
  if (op == TFTP_OP_ERROR) {
    return -1;
  }

  if (op != TFTP_OP_DATA) {
    return -1;
  }

  block = (UINT16)(((UINT16)t->rx[2] << 8) | t->rx[3]);
  if (block != t->expect_block) {
    /* Duplicate/out-of-order: re-ACK last good block. */
    if (block + 1u == t->expect_block && t->expect_block > 1u) {
      (VOID)TftpSendAck (t, (UINT16)(t->expect_block - 1u));
    }

    return 0;
  }

  data_len = (UINT16)(t->rx_len - TFTP_HDR);
  if (t->got + data_len > t->dest_cap) {
    return -1;
  }

  dst = (UINT8 *)t->dest + t->got;
  CopyMem (dst, t->rx + TFTP_HDR, data_len);
  t->got += data_len;
  if (TftpSendAck (t, block) != 0) {
    return -1;
  }

  t->expect_block = (UINT16)(block + 1u);
  t->retries      = 0;
  t->last_block   = (data_len < TFTP_BLOCK) ? 1 : 0;
  return 0;
}

STATIC
pm_metal_status_t
TftpCoro (
  pm_metal_coro_t  *self
  )
{
  tftp_get_t  *t;

  t = (tftp_get_t *)self;

  switch (t->step) {
  case TFTP_STEP_RESOLVE:
    t->got          = 0;
    t->status       = 1;
    t->pcb          = NULL;
    t->expect_block = 1;
    t->retries      = 0;
    t->have_pkt     = 0;
    t->last_block   = 0;
    t->server_port  = TFTP_PORT;

    if (t->host[0] == '\0') {
      CHAR8  boot[PM_METAL_NET_BOOT_FILE_MAX];

      if (pm_metal_net_if_boot_get (
            NULL,
            t->host,
            sizeof (t->host),
            boot,
            sizeof (boot)
            ) != 0
          || t->host[0] == '\0')
      {
        t->status = 2;
        t->step   = TFTP_STEP_DONE;
        break;
      }

      if (t->path[0] == '\0' && boot[0] != '\0') {
        AsciiStrCpyS (t->path, sizeof (t->path), boot);
      }
    }

    if (t->path[0] == '\0' || t->dest == NULL || t->dest_cap == 0) {
      t->status = 3;
      t->step   = TFTP_STEP_DONE;
      break;
    }

    {
      UINT32  hip;

      if (pm_metal_net_resolve_ip4 (t->host, &hip) == 0) {
        IP_ADDR4 (
          &t->server,
          (hip >> 24) & 0xffu,
          (hip >> 16) & 0xffu,
          (hip >> 8) & 0xffu,
          hip & 0xffu
          );
        t->step = TFTP_STEP_OPEN;
        break;
      }
    }

    t->aw = pm_metal_net_dns (t->host);
    if (t->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      t->status = 4;
      t->step   = TFTP_STEP_DONE;
      break;
    }

    t->step = TFTP_STEP_DNS_AW;
    return (pm_metal_status_t)pm_metal_async_await_coro (self, t->aw);

  case TFTP_STEP_DNS_AW:
    if ((UINT32)(UINTN)self->result == 0) {
      t->status = 4;
      t->step   = TFTP_STEP_DONE;
      break;
    }

    {
      CHAR8       ipstr[64];
      ip4_addr_t  a4;

      if (pm_metal_net_dns_last_ntoa (ipstr, sizeof (ipstr)) != 0
          || ip4addr_aton (ipstr, &a4) == 0)
      {
        t->status = 4;
        t->step   = TFTP_STEP_DONE;
        break;
      }

      ip_addr_copy_from_ip4 (t->server, a4);
    }

    t->step = TFTP_STEP_OPEN;
    break;

  case TFTP_STEP_OPEN:
    t->pcb = udp_new ();
    if (t->pcb == NULL) {
      t->status = 5;
      t->step   = TFTP_STEP_DONE;
      break;
    }

    udp_recv (t->pcb, TftpRecvCb, t);
    if (udp_bind (t->pcb, IP_ANY_TYPE, 0) != ERR_OK) {
      TftpTeardown (t);
      t->status = 5;
      t->step   = TFTP_STEP_DONE;
      break;
    }

    t->step = TFTP_STEP_SEND;
    break;

  case TFTP_STEP_SEND:
    t->have_pkt = 0;
    if (TftpSendRrq (t) != 0) {
      TftpTeardown (t);
      t->status = 6;
      t->step   = TFTP_STEP_DONE;
      break;
    }

    t->deadline = pm_metal_time_mono_us () + TFTP_TIMEOUT_US;
    t->step     = TFTP_STEP_WAIT;
    break;

  case TFTP_STEP_WAIT:
    pm_metal_net_poll ();
    if (t->have_pkt) {
      t->have_pkt = 0;
      if (TftpHandlePkt (t) != 0) {
        TftpTeardown (t);
        t->status = 7;
        t->step   = TFTP_STEP_DONE;
        break;
      }

      if (t->last_block) {
        TftpTeardown (t);
        t->status = 0;
        t->step   = TFTP_STEP_DONE;
        break;
      }

      t->deadline = pm_metal_time_mono_us () + TFTP_TIMEOUT_US;
      break;
    }

    if (pm_metal_time_mono_us () >= t->deadline) {
      t->retries++;
      if (t->retries > TFTP_RETRIES) {
        TftpTeardown (t);
        t->status = 8;
        t->step   = TFTP_STEP_DONE;
        break;
      }

      if (t->expect_block == 1) {
        t->step = TFTP_STEP_SEND;
      } else {
        (VOID)TftpSendAck (t, (UINT16)(t->expect_block - 1u));
        t->deadline = pm_metal_time_mono_us () + TFTP_TIMEOUT_US;
      }

      break;
    }

    return pm_metal_await (self, pm_metal_sleep_us (2000));

  case TFTP_STEP_DONE:
  default:
    TftpTeardown (t);
    mTftpLastDone.valid  = 1;
    mTftpLastDone.status = t->status;
    mTftpLastDone.len    = t->got;
    self->result         = (VOID *)(UINTN)(t->status == 0 ? 1u : 0u);
    return (t->status == 0) ? PM_METAL_DONE : PM_METAL_ERROR;
  }

  return PM_METAL_PENDING;
}

pm_metal_async_handle_t
pm_metal_net_tftp_get (
  CONST CHAR8  *host,
  CONST CHAR8  *path,
  VOID         *dest,
  UINT32        dest_cap
  )
{
  tftp_get_t  *t;

  if (dest == NULL || dest_cap == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  t = (tftp_get_t *)pm_metal_coro (TftpCoro, sizeof (*t));
  if (t == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  mTftpLastDone.valid = 0;
  t->step     = TFTP_STEP_RESOLVE;
  t->dest     = dest;
  t->dest_cap = dest_cap;
  t->host[0]  = '\0';
  t->path[0]  = '\0';
  if (host != NULL && host[0] != '\0') {
    AsciiStrCpyS (t->host, sizeof (t->host), host);
  }

  if (path != NULL && path[0] != '\0') {
    AsciiStrCpyS (t->path, sizeof (t->path), path);
  }

  return pm_metal_async_adopt_host_coro (&t->coro);
}

uint32_t
pm_metal_net_tftp_status (
  pm_metal_async_handle_t  h
  )
{
  (VOID)h;
  return mTftpLastDone.valid ? mTftpLastDone.status : 1u;
}

uint32_t
pm_metal_net_tftp_len (
  pm_metal_async_handle_t  h
  )
{
  (VOID)h;
  return mTftpLastDone.valid ? mTftpLastDone.len : 0u;
}

void
pm_metal_net_tftp_bind_inst (
  VOID  *module_inst
  )
{
  mTftpInst = (wasm_module_inst_t)module_inst;
}

STATIC
pm_metal_async_handle_t
pm_metal_net_tftp_get_native (
  wasm_exec_env_t  exec_env,
  CHAR8           *host,
  CHAR8           *path,
  UINT32           dest,
  UINT32           dest_cap
  )
{
  VOID  *native;

  (VOID)exec_env;
  if (mTftpInst == NULL || dest_cap == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (!wasm_runtime_validate_app_addr (mTftpInst, dest, dest_cap)) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  native = wasm_runtime_addr_app_to_native (mTftpInst, dest);
  if (native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_net_tftp_get (host, path, native, dest_cap);
}

STATIC
UINT32
pm_metal_net_tftp_status_native (
  wasm_exec_env_t          exec_env,
  pm_metal_async_handle_t  h
  )
{
  (VOID)exec_env;
  return pm_metal_net_tftp_status (h);
}

STATIC
UINT32
pm_metal_net_tftp_len_native (
  wasm_exec_env_t          exec_env,
  pm_metal_async_handle_t  h
  )
{
  (VOID)exec_env;
  return pm_metal_net_tftp_len (h);
}

STATIC NativeSymbol g_pm_metal_net_tftp_native_symbols[] = {
  { "pm_metal_net_tftp_get", (VOID *)pm_metal_net_tftp_get_native, "($$ii)i",
    NULL },
  { "pm_metal_net_tftp_status", (VOID *)pm_metal_net_tftp_status_native, "(i)i",
    NULL },
  { "pm_metal_net_tftp_len", (VOID *)pm_metal_net_tftp_len_native, "(i)i",
    NULL },
};

int
pm_metal_net_tftp_native_register (
  VOID
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_NET_TFTP_WASI_MODULE,
         g_pm_metal_net_tftp_native_symbols,
         sizeof (g_pm_metal_net_tftp_native_symbols)
           / sizeof (g_pm_metal_net_tftp_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}
