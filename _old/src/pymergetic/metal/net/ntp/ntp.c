/** @file
  SNTP client over lwIP UDP (async host coro + guest imports).
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/net/ntp/ntp.h>
#include <pymergetic/metal/net/ip/ip.h>
#include <pymergetic/metal/net/ip/ip_cfg.h>
#include <pymergetic/metal/net/ip/ip_ops.h>
#include <pymergetic/metal/dev/random/random.h>
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

#define NTP_PORT       123u
#define NTP_PKT_LEN    48u
#define NTP_HOST_MAX   128u
#define NTP_TIMEOUT_US 3000000ull
#define NTP_RETRIES    3u
#define NTP_UNIX_EPOCH 2208988800ull /* 1900→1970 seconds */

typedef enum {
  NTP_STEP_RESOLVE = 0,
  NTP_STEP_DNS_AW,
  NTP_STEP_OPEN,
  NTP_STEP_SEND,
  NTP_STEP_WAIT,
  NTP_STEP_DONE
} ntp_step_t;

typedef struct {
  ntp_step_t              step;
  pm_metal_async_handle_t aw;
  char                    host[NTP_HOST_MAX];
  uint32_t                status;
  uint64_t                unix_ms;
  ip_addr_t               server;
  struct udp_pcb         *pcb;
  uint32_t                retries;
  uint64_t                deadline;
  int32_t                 have_pkt;
  uint8_t                 rx[NTP_PKT_LEN];
} ntp_sync_t;

static struct {
  int32_t  valid;
  uint32_t status;
  uint64_t unix_ms;
} mNtpLastDone;

static void NtpTeardown(ntp_sync_t *t)
{
  if (t == NULL) {
    return;
  }

  if (t->pcb != NULL) {
    udp_remove(t->pcb);
    t->pcb = NULL;
  }
}

static void NtpRecvCb(
  void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
  ntp_sync_t *t;

  (void)pcb;
  (void)addr;
  (void)port;
  t = (ntp_sync_t *)arg;
  if (t == NULL || p == NULL) {
    if (p != NULL) {
      pbuf_free(p);
    }

    return;
  }

  if (!t->have_pkt && p->tot_len >= NTP_PKT_LEN) {
    if (pbuf_copy_partial(p, t->rx, NTP_PKT_LEN, 0) == NTP_PKT_LEN) {
      t->have_pkt = 1;
    }
  }

  pbuf_free(p);
}

static int32_t NtpParseReply(const uint8_t *pkt, uint64_t *out_unix_ms)
{
  uint32_t sec_be;
  uint32_t frac_be;
  uint32_t sec;
  uint32_t frac;
  uint8_t  mode;
  uint64_t unix_s;
  uint64_t ms;

  if (pkt == NULL || out_unix_ms == NULL) {
    return -1;
  }

  mode = (uint8_t)(pkt[0] & 0x7u);
  if (mode != 4u && mode != 5u) {
    /* server (4) or broadcast (5); accept either */
    return -1;
  }

  memcpy(&sec_be, pkt + 40, 4);
  memcpy(&frac_be, pkt + 44, 4);
  sec  = ntohl(sec_be);
  frac = ntohl(frac_be);
  if (sec < NTP_UNIX_EPOCH) {
    return -1;
  }

  unix_s       = (uint64_t)sec - NTP_UNIX_EPOCH;
  ms           = unix_s * 1000ull + ((uint64_t)frac * 1000ull) / 4294967296ull;
  *out_unix_ms = ms;
  return 0;
}

static int32_t NtpSendQuery(ntp_sync_t *t)
{
  struct pbuf *p;
  uint8_t     *q;

  if (t == NULL || t->pcb == NULL) {
    return -1;
  }

  p = pbuf_alloc(PBUF_TRANSPORT, NTP_PKT_LEN, PBUF_RAM);
  if (p == NULL || p->payload == NULL) {
    if (p != NULL) {
      pbuf_free(p);
    }

    return -1;
  }

  q = (uint8_t *)p->payload;
  memset(q, 0, NTP_PKT_LEN);
  /* LI=0 VN=4 Mode=3 (client) */
  q[0] = 0x23u;

  if (udp_sendto(t->pcb, p, &t->server, NTP_PORT) != ERR_OK) {
    pbuf_free(p);
    return -1;
  }

  pbuf_free(p);
  return 0;
}

static pm_metal_status_t NtpStep(pm_metal_async_handle_t self_h)
{
  ntp_sync_t *t;

  t = (ntp_sync_t *)(uintptr_t)pm_metal_async_coro_state(self_h);
  if (t == NULL) {
    return PM_METAL_ERROR;
  }

  for (;;) {
    switch (t->step) {
    case NTP_STEP_RESOLVE:
      t->status   = 1;
      t->unix_ms  = 0;
      t->have_pkt = 0;
      t->retries  = 0;
      t->aw       = PM_METAL_ASYNC_HANDLE_INVALID;

      if (t->host[0] == '\0') {
        pm_metal_net_ip_ifcfg_t cfg;

        if (pm_metal_net_ip_if_get(&cfg) == 0 && cfg.ntp[0] != '\0') {
          snprintf(t->host, sizeof(t->host), "%s", cfg.ntp);
        }
      }

      if (t->host[0] == '\0') {
        t->status = 2;
        t->step   = NTP_STEP_DONE;
        break;
      }

      {
        uint32_t hip;

        if (pm_metal_net_ip_resolve_ip4(t->host, &hip) == 0) {
          IP_ADDR4(
            &t->server, (hip >> 24) & 0xffu, (hip >> 16) & 0xffu, (hip >> 8) & 0xffu, hip & 0xffu);
          t->step = NTP_STEP_OPEN;
          break;
        }
      }

      t->aw = pm_metal_net_ip_dns(t->host);
      if (t->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
        t->status = 3;
        t->step   = NTP_STEP_DONE;
        break;
      }

      t->step = NTP_STEP_DNS_AW;
      return pm_metal_async_await(self_h, t->aw);

    case NTP_STEP_DNS_AW:
      if (pm_metal_async_result_u32(self_h) == 0) {
        t->status = 3;
        t->step   = NTP_STEP_DONE;
        break;
      }

      {
        char       ipstr[64];
        ip4_addr_t a4;

        if (pm_metal_net_ip_dns_last_ntoa(ipstr, sizeof(ipstr)) != 0 ||
            ip4addr_aton(ipstr, &a4) == 0) {
          t->status = 3;
          t->step   = NTP_STEP_DONE;
          break;
        }

        ip_addr_copy_from_ip4(t->server, a4);
      }

      t->step = NTP_STEP_OPEN;
      break;

    case NTP_STEP_OPEN:
      t->pcb = udp_new();
      if (t->pcb == NULL) {
        t->status = 4;
        t->step   = NTP_STEP_DONE;
        break;
      }

      udp_recv(t->pcb, NtpRecvCb, t);
      if (udp_bind(t->pcb, IP_ANY_TYPE, 0) != ERR_OK) {
        NtpTeardown(t);
        t->status = 4;
        t->step   = NTP_STEP_DONE;
        break;
      }

      t->step = NTP_STEP_SEND;
      break;

    case NTP_STEP_SEND:
      t->have_pkt = 0;
      if (NtpSendQuery(t) != 0) {
        NtpTeardown(t);
        t->status = 5;
        t->step   = NTP_STEP_DONE;
        break;
      }

      t->deadline = pm_metal_time_mono_us() + NTP_TIMEOUT_US;
      t->step     = NTP_STEP_WAIT;
      break;

    case NTP_STEP_WAIT:
      pm_metal_net_ip_poll();
      if (t->have_pkt) {
        if (NtpParseReply(t->rx, &t->unix_ms) != 0) {
          NtpTeardown(t);
          t->status = 6;
          t->step   = NTP_STEP_DONE;
          break;
        }

        pm_metal_realtime_set_unix_ms(t->unix_ms);
        NtpTeardown(t);
        t->status = 0;
        t->step   = NTP_STEP_DONE;
        break;
      }

      if (pm_metal_time_mono_us() >= t->deadline) {
        t->retries++;
        if (t->retries > NTP_RETRIES) {
          NtpTeardown(t);
          t->status = 7;
          t->step   = NTP_STEP_DONE;
          break;
        }

        t->step = NTP_STEP_SEND;
        break;
      }

      return pm_metal_async_await(self_h, pm_metal_async_sleep_us(2000));

    case NTP_STEP_DONE:
      mNtpLastDone.valid   = 1;
      mNtpLastDone.status  = t->status;
      mNtpLastDone.unix_ms = t->unix_ms;
      pm_metal_async_set_result_u32(self_h, t->status == 0 ? 1u : 0u);
      return PM_METAL_DONE;

    default:
      return PM_METAL_ERROR;
    }
  }
}

static void NtpRelease(void *state)
{
  NtpTeardown((ntp_sync_t *)state);
}

pm_metal_async_handle_t pm_metal_net_ntp_sync(const char *host)
{
  ntp_sync_t             *t;
  pm_metal_async_handle_t h;

  h = pm_metal_async_coro_create(NtpStep, sizeof(*t));
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  t = (ntp_sync_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (t == NULL) {
    pm_metal_async_coro_close(h);
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  mNtpLastDone.valid = 0;
  t->step            = NTP_STEP_RESOLVE;
  t->host[0]         = '\0';
  if (host != NULL && host[0] != '\0') {
    snprintf(t->host, sizeof(t->host), "%s", host);
  }

  pm_metal_async_coro_set_release(h, NtpRelease);
  return h;
}

uint32_t pm_metal_net_ntp_status(pm_metal_async_handle_t h)
{
  ntp_sync_t *t;

  t = (ntp_sync_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (t != NULL) {
    return t->status;
  }

  return mNtpLastDone.valid ? mNtpLastDone.status : 1u;
}

uint64_t pm_metal_net_ntp_last_unix_ms(void)
{
  return mNtpLastDone.valid ? mNtpLastDone.unix_ms : 0ull;
}

static pm_metal_async_handle_t pm_metal_net_ntp_sync_native(wasm_exec_env_t exec_env, char *host)
{
  (void)exec_env;
  return pm_metal_net_ntp_sync(host);
}

static uint32_t pm_metal_net_ntp_status_native(wasm_exec_env_t exec_env, pm_metal_async_handle_t h)
{
  (void)exec_env;
  return pm_metal_net_ntp_status(h);
}

static uint64_t pm_metal_net_ntp_last_unix_ms_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_net_ntp_last_unix_ms();
}

static NativeSymbol g_pm_metal_net_ntp_native_symbols[] = {
  { "pm_metal_net_ntp_sync", (void *)pm_metal_net_ntp_sync_native, "($)i", NULL },
  { "pm_metal_net_ntp_status", (void *)pm_metal_net_ntp_status_native, "(i)i", NULL },
  { "pm_metal_net_ntp_last_unix_ms", (void *)pm_metal_net_ntp_last_unix_ms_native, "()I", NULL },
};

int pm_metal_net_ntp_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_NET_NTP_WASI_MODULE,
                                     g_pm_metal_net_ntp_native_symbols,
                                     sizeof(g_pm_metal_net_ntp_native_symbols) /
                                       sizeof(g_pm_metal_net_ntp_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
