/** @file
  TFTP RRQ client over lwIP UDP (async host coro + guest imports).
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/net/tftp/tftp.h>
#include <pymergetic/metal/net/ip/ip.h>
#include <pymergetic/metal/net/ip/ip_cfg.h>
#include <pymergetic/metal/net/ip/ip_ops.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/util/ip.h>
#include <runtime/time/time.h>

#include "lwipopts.h" /* IWYU pragma: keep */
#include <lwip/udp.h>
#include <lwip/pbuf.h>
#include <lwip/ip_addr.h>
#include <lwip/dns.h>
#include <lwip/inet.h>

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
  tftp_step_t             step;
  pm_metal_async_handle_t aw;
  char                    host[TFTP_HOST_MAX];
  char                    path[TFTP_PATH_MAX];
  void                   *dest;
  uint32_t                dest_cap;
  uint32_t                got;
  uint32_t                status;
  ip_addr_t               server;
  uint16_t                server_port;
  struct udp_pcb         *pcb;
  uint16_t                expect_block;
  uint32_t                retries;
  uint64_t                deadline;
  int32_t                 have_pkt;
  uint8_t                 rx[TFTP_PKT_MAX];
  uint16_t                rx_len;
  int32_t                 last_block;
} tftp_get_t;

static struct {
  int32_t  valid;
  uint32_t status;
  uint32_t len;
} mTftpLastDone;

static void TftpTeardown(tftp_get_t *t)
{
  if (t == NULL) {
    return;
  }

  if (t->pcb != NULL) {
    udp_remove(t->pcb);
    t->pcb = NULL;
  }
}

static void TftpRecvCb(
  void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, uint16_t port)
{
  tftp_get_t *t;
  uint16_t    n;

  (void)pcb;
  t = (tftp_get_t *)arg;
  if (t == NULL || p == NULL) {
    if (p != NULL) {
      pbuf_free(p);
    }

    return;
  }

  if (t->have_pkt) {
    pbuf_free(p);
    return;
  }

  if (addr != NULL) {
    t->server      = *addr;
    t->server_port = port;
  }

  n = (uint16_t)(p->tot_len < sizeof(t->rx) ? p->tot_len : sizeof(t->rx));
  pbuf_copy_partial(p, t->rx, n, 0);
  t->rx_len   = n;
  t->have_pkt = 1;
  pbuf_free(p);
}

static int32_t TftpSendRrq(tftp_get_t *t)
{
  uint8_t      pkt[TFTP_PKT_MAX];
  uint16_t     off;
  uintptr_t    plen;
  uintptr_t    mlen;
  struct pbuf *p;
  err_t        e;

  if (t == NULL || t->pcb == NULL) {
    return -1;
  }

  pkt[0] = 0;
  pkt[1] = (uint8_t)TFTP_OP_RRQ;
  off    = 2;
  plen   = strlen(t->path);
  if (plen == 0 || off + plen + 1u + 6u >= sizeof(pkt)) {
    return -1;
  }

  memcpy(pkt + off, t->path, plen);
  off        = (uint16_t)(off + plen);
  pkt[off++] = 0;
  mlen       = 5; /* "octet" */
  memcpy(pkt + off, "octet", mlen);
  off        = (uint16_t)(off + mlen);
  pkt[off++] = 0;

  p = pbuf_alloc(PBUF_TRANSPORT, off, PBUF_RAM);
  if (p == NULL) {
    return -1;
  }

  memcpy(p->payload, pkt, off);
  e = udp_sendto(t->pcb, p, &t->server, TFTP_PORT);
  pbuf_free(p);
  return (e == ERR_OK) ? 0 : -1;
}

static int32_t TftpSendAck(tftp_get_t *t, uint16_t block)
{
  uint8_t      pkt[4];
  struct pbuf *p;
  err_t        e;

  if (t == NULL || t->pcb == NULL) {
    return -1;
  }

  pkt[0] = 0;
  pkt[1] = (uint8_t)TFTP_OP_ACK;
  pkt[2] = (uint8_t)(block >> 8);
  pkt[3] = (uint8_t)(block & 0xffu);

  p = pbuf_alloc(PBUF_TRANSPORT, 4, PBUF_RAM);
  if (p == NULL) {
    return -1;
  }

  memcpy(p->payload, pkt, 4);
  e = udp_sendto(t->pcb, p, &t->server, t->server_port);
  pbuf_free(p);
  return (e == ERR_OK) ? 0 : -1;
}

static int32_t TftpHandlePkt(tftp_get_t *t)
{
  uint16_t op;
  uint16_t block;
  uint16_t data_len;
  uint8_t *dst;

  if (t == NULL || t->rx_len < TFTP_HDR) {
    return -1;
  }

  op = (uint16_t)(((uint16_t)t->rx[0] << 8) | t->rx[1]);
  if (op == TFTP_OP_ERROR) {
    return -1;
  }

  if (op != TFTP_OP_DATA) {
    return -1;
  }

  block = (uint16_t)(((uint16_t)t->rx[2] << 8) | t->rx[3]);
  if (block != t->expect_block) {
    /* Duplicate/out-of-order: re-ACK last good block. */
    if (block + 1u == t->expect_block && t->expect_block > 1u) {
      (void)TftpSendAck(t, (uint16_t)(t->expect_block - 1u));
    }

    return 0;
  }

  data_len = (uint16_t)(t->rx_len - TFTP_HDR);
  if (t->got + data_len > t->dest_cap) {
    return -1;
  }

  dst = (uint8_t *)t->dest + t->got;
  memcpy(dst, t->rx + TFTP_HDR, data_len);
  t->got += data_len;
  if (TftpSendAck(t, block) != 0) {
    return -1;
  }

  t->expect_block = (uint16_t)(block + 1u);
  t->retries      = 0;
  t->last_block   = (data_len < TFTP_BLOCK) ? 1 : 0;
  return 0;
}

static pm_metal_status_t TftpStep(pm_metal_async_handle_t self_h)
{
  tftp_get_t *t;

  t = (tftp_get_t *)(uintptr_t)pm_metal_async_coro_state(self_h);
  if (t == NULL) {
    return PM_METAL_ERROR;
  }

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
      char boot[PM_METAL_NET_IP_BOOT_FILE_MAX];

      if (pm_metal_net_ip_if_boot_get(NULL, t->host, sizeof(t->host), boot, sizeof(boot)) != 0 ||
          t->host[0] == '\0') {
        t->status = 2;
        t->step   = TFTP_STEP_DONE;
        break;
      }

      if (t->path[0] == '\0' && boot[0] != '\0') {
        snprintf(t->path, sizeof(t->path), "%s", boot);
      }
    }

    if (t->path[0] == '\0' || t->dest == NULL || t->dest_cap == 0) {
      t->status = 3;
      t->step   = TFTP_STEP_DONE;
      break;
    }

    {
      uint32_t hip;

      if (pm_metal_net_ip_resolve_ip4(t->host, &hip) == 0) {
        IP_ADDR4(
          &t->server, (hip >> 24) & 0xffu, (hip >> 16) & 0xffu, (hip >> 8) & 0xffu, hip & 0xffu);
        t->step = TFTP_STEP_OPEN;
        break;
      }
    }

    t->aw = pm_metal_net_ip_dns(t->host);
    if (t->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      t->status = 4;
      t->step   = TFTP_STEP_DONE;
      break;
    }

    t->step = TFTP_STEP_DNS_AW;
    return pm_metal_async_await(self_h, t->aw);

  case TFTP_STEP_DNS_AW:
    if (pm_metal_async_result_u32(self_h) == 0) {
      t->status = 4;
      t->step   = TFTP_STEP_DONE;
      break;
    }

    {
      char       ipstr[64];
      ip4_addr_t a4;

      if (pm_metal_net_ip_dns_last_ntoa(ipstr, sizeof(ipstr)) != 0 ||
          ip4addr_aton(ipstr, &a4) == 0) {
        t->status = 4;
        t->step   = TFTP_STEP_DONE;
        break;
      }

      ip_addr_copy_from_ip4(t->server, a4);
    }

    t->step = TFTP_STEP_OPEN;
    break;

  case TFTP_STEP_OPEN:
    t->pcb = udp_new();
    if (t->pcb == NULL) {
      t->status = 5;
      t->step   = TFTP_STEP_DONE;
      break;
    }

    udp_recv(t->pcb, TftpRecvCb, t);
    if (udp_bind(t->pcb, IP_ANY_TYPE, 0) != ERR_OK) {
      TftpTeardown(t);
      t->status = 5;
      t->step   = TFTP_STEP_DONE;
      break;
    }

    t->step = TFTP_STEP_SEND;
    break;

  case TFTP_STEP_SEND:
    t->have_pkt = 0;
    if (TftpSendRrq(t) != 0) {
      TftpTeardown(t);
      t->status = 6;
      t->step   = TFTP_STEP_DONE;
      break;
    }

    t->deadline = pm_metal_time_mono_us() + TFTP_TIMEOUT_US;
    t->step     = TFTP_STEP_WAIT;
    break;

  case TFTP_STEP_WAIT:
    pm_metal_net_ip_poll();
    if (t->have_pkt) {
      t->have_pkt = 0;
      if (TftpHandlePkt(t) != 0) {
        TftpTeardown(t);
        t->status = 7;
        t->step   = TFTP_STEP_DONE;
        break;
      }

      if (t->last_block) {
        TftpTeardown(t);
        t->status = 0;
        t->step   = TFTP_STEP_DONE;
        break;
      }

      t->deadline = pm_metal_time_mono_us() + TFTP_TIMEOUT_US;
      break;
    }

    if (pm_metal_time_mono_us() >= t->deadline) {
      t->retries++;
      if (t->retries > TFTP_RETRIES) {
        TftpTeardown(t);
        t->status = 8;
        t->step   = TFTP_STEP_DONE;
        break;
      }

      if (t->expect_block == 1) {
        t->step = TFTP_STEP_SEND;
      } else {
        (void)TftpSendAck(t, (uint16_t)(t->expect_block - 1u));
        t->deadline = pm_metal_time_mono_us() + TFTP_TIMEOUT_US;
      }

      break;
    }

    return pm_metal_async_await(self_h, pm_metal_async_sleep_us(2000));

  case TFTP_STEP_DONE:
  default:
    TftpTeardown(t);
    mTftpLastDone.valid  = 1;
    mTftpLastDone.status = t->status;
    mTftpLastDone.len    = t->got;
    pm_metal_async_set_result_u32(self_h, t->status == 0 ? 1u : 0u);
    return (t->status == 0) ? PM_METAL_DONE : PM_METAL_ERROR;
  }

  return PM_METAL_PENDING;
}

static void TftpRelease(void *state)
{
  TftpTeardown((tftp_get_t *)state);
}

pm_metal_async_handle_t pm_metal_net_tftp_get(const char *host,
                                              const char *path,
                                              void       *dest,
                                              uint32_t    dest_cap)
{
  tftp_get_t             *t;
  pm_metal_async_handle_t h;

  if (dest == NULL || dest_cap == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  h = pm_metal_async_coro_create(TftpStep, sizeof(*t));
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  t = (tftp_get_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (t == NULL) {
    pm_metal_async_coro_close(h);
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  mTftpLastDone.valid = 0;
  t->step             = TFTP_STEP_RESOLVE;
  t->dest             = dest;
  t->dest_cap         = dest_cap;
  t->host[0]          = '\0';
  t->path[0]          = '\0';
  if (host != NULL && host[0] != '\0') {
    snprintf(t->host, sizeof(t->host), "%s", host);
  }

  if (path != NULL && path[0] != '\0') {
    snprintf(t->path, sizeof(t->path), "%s", path);
  }

  pm_metal_async_coro_set_release(h, TftpRelease);
  return h;
}

uint32_t pm_metal_net_tftp_status(pm_metal_async_handle_t h)
{
  tftp_get_t *t;

  t = (tftp_get_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (t != NULL) {
    return t->status;
  }

  return mTftpLastDone.valid ? mTftpLastDone.status : 1u;
}

uint32_t pm_metal_net_tftp_len(pm_metal_async_handle_t h)
{
  tftp_get_t *t;

  t = (tftp_get_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (t != NULL) {
    return t->got;
  }

  return mTftpLastDone.valid ? mTftpLastDone.len : 0u;
}

static pm_metal_async_handle_t pm_metal_net_tftp_get_native(
  wasm_exec_env_t exec_env, char *host, char *path, uint32_t dest, uint32_t dest_cap)
{
  void *native;

  if (dest_cap == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  native = pm_metal_async_guest_buf_durable(exec_env, dest, dest_cap);
  if (native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_net_tftp_get(host, path, native, dest_cap);
}

static uint32_t pm_metal_net_tftp_status_native(wasm_exec_env_t exec_env, pm_metal_async_handle_t h)
{
  (void)exec_env;
  return pm_metal_net_tftp_status(h);
}

static uint32_t pm_metal_net_tftp_len_native(wasm_exec_env_t exec_env, pm_metal_async_handle_t h)
{
  (void)exec_env;
  return pm_metal_net_tftp_len(h);
}

static NativeSymbol g_pm_metal_net_tftp_native_symbols[] = {
  { "pm_metal_net_tftp_get", (void *)pm_metal_net_tftp_get_native, "($$ii)i", NULL },
  { "pm_metal_net_tftp_status", (void *)pm_metal_net_tftp_status_native, "(i)i", NULL },
  { "pm_metal_net_tftp_len", (void *)pm_metal_net_tftp_len_native, "(i)i", NULL },
};

int pm_metal_net_tftp_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_NET_TFTP_WASI_MODULE,
                                     g_pm_metal_net_tftp_native_symbols,
                                     sizeof(g_pm_metal_net_tftp_native_symbols) /
                                       sizeof(g_pm_metal_net_tftp_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
