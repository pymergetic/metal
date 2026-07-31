/** @file
  ICMP echo (ping) — async coro over lwIP raw PCBs.
  (impl: efi|bios)
**/
#include <stddef.h>
#include <stdio.h>

#include <pymergetic/metal/net/ping/ping.h>
#include <pymergetic/metal/net/ip/ip.h>
#include <pymergetic/metal/net/ip/ip_cfg.h>
#include <pymergetic/metal/net/ip/ip_ops.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <runtime/time/time.h>

#include "lwipopts.h" /* IWYU pragma: keep */
#include <lwip/icmp.h>
#include <lwip/prot/ip4.h>
#include <lwip/prot/icmp6.h>
#include <lwip/inet_chksum.h>
#include <lwip/ip4.h>
#include <lwip/ip6.h>
#include <lwip/ip_addr.h>
#include <lwip/pbuf.h>
#include <lwip/raw.h>
#include <lwip/netif.h>
#include <lwip/err.h>

#include "wasm_export.h"

#include <stdint.h>

#define PING_HOST_MAX   128u
#define PING_ID         0x4d54u
#define PING_PROTO_V4   1u  /* IP_PROTO_ICMP */
#define PING_PROTO_V6   58u /* IPv6 next header: ICMPv6 */
#define PING_DATA_SIZE  32u
#define PING_SEND_TRIES 40u /* ~ARP wait: 40 * 25ms */

typedef enum {
  PING_STEP_RESOLVE = 0,
  PING_STEP_DNS_AW,
  PING_STEP_OPEN,
  PING_STEP_SEND,
  PING_STEP_WAIT,
  PING_STEP_DONE
} ping_step_t;

typedef struct {
  ping_step_t             step;
  pm_metal_async_handle_t aw;
  char                    host[PING_HOST_MAX];
  uint32_t                timeout_ms;
  uint64_t                deadline;
  struct raw_pcb         *pcb;
  ip_addr_t               target;
  int32_t                 target_v6;
  uint16_t                seq;
  uint64_t                send_us;
  uint32_t                rtt_us;
  uint32_t                send_tries;
  err_t                   last_err;
  int32_t                 ok;
} ping_ctx_t;

static struct {
  int32_t  valid;
  uint32_t rtt_us;
  uint32_t err;
} mPingLastDone;

static uint32_t mPingLastErr;

static int32_t PingParseIpv4(const char *s, ip4_addr_t *out)
{
  uint32_t    a;
  uint32_t    b;
  uint32_t    c;
  uint32_t    d;
  uint32_t    v;
  const char *p;
  uint32_t   *n;

  if (s == NULL || out == NULL) {
    return -1;
  }

  a = b = c = d = 0;
  p             = s;
  n             = &a;
  v             = 0;
  for (;;) {
    if (*p >= '0' && *p <= '9') {
      v = v * 10u + (uint32_t)(*p - '0');
      if (v > 255) {
        return -1;
      }

      p++;
      continue;
    }

    *n = v;
    if (*p == '.') {
      if (n == &a) {
        n = &b;
      } else if (n == &b) {
        n = &c;
      } else if (n == &c) {
        n = &d;
      } else {
        return -1;
      }

      v = 0;
      p++;
      continue;
    }

    if (*p == '\0') {
      if (n != &d) {
        return -1;
      }

      *n = v;
      IP4_ADDR(out, a, b, c, d);
      return 0;
    }

    return -1;
  }
}

static int32_t PingParseIpv6(const char *s, ip6_addr_t *out)
{
  if (s == NULL || out == NULL) {
    return -1;
  }

  return ip6addr_aton(s, out) ? 0 : -1;
}

static int32_t PingHostIsLiteral(const char *host, ip_addr_t *out, int32_t *v6)
{
  ip4_addr_t a4;
  ip6_addr_t a6;

  if (host == NULL || out == NULL || v6 == NULL) {
    return -1;
  }

  if (PingParseIpv4(host, &a4) == 0) {
    ip_addr_copy_from_ip4(*out, a4);
    *v6 = 0;
    return 0;
  }

  if (PingParseIpv6(host, &a6) == 0) {
    ip_addr_copy_from_ip6(*out, a6);
    *v6 = 1;
    return 0;
  }

  return -1;
}

static u8_t PingRawProto(ping_ctx_t *p)
{
  return p->target_v6 ? PING_PROTO_V6 : PING_PROTO_V4;
}

static void PingCleanup(ping_ctx_t *p)
{
  if (p == NULL) {
    return;
  }

  if (p->pcb != NULL) {
    raw_remove(p->pcb);
    p->pcb = NULL;
  }
}

static u8_t PingRecv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
  ping_ctx_t            *ctx;
  struct icmp_echo_hdr  *iecho;
  struct icmp6_echo_hdr *ie6;

  (void)pcb;
  (void)addr;
  ctx = (ping_ctx_t *)arg;
  if (ctx == NULL || p == NULL || ctx->ok) {
    return 0;
  }

  if (!ctx->target_v6) {
    const struct ip_hdr *iphdr;
    u16_t                hlen;

    iphdr = ip4_current_header();
    if (iphdr == NULL) {
      return 0;
    }

    hlen = IPH_HL_BYTES(iphdr);
    if (p->tot_len < (hlen + sizeof(struct icmp_echo_hdr))) {
      return 0;
    }

    if (pbuf_remove_header(p, hlen) != 0) {
      return 0;
    }

    iecho = (struct icmp_echo_hdr *)p->payload;
    if ((iecho->id != PING_ID) || (iecho->seqno != lwip_htons(ctx->seq)) ||
        (ICMPH_TYPE(iecho) != ICMP_ER)) {
      pbuf_add_header(p, hlen);
      return 0;
    }
  } else {
    if (p->tot_len < (IP6_HLEN + sizeof(struct icmp6_echo_hdr))) {
      return 0;
    }

    if (pbuf_remove_header(p, IP6_HLEN) != 0) {
      return 0;
    }

    ie6 = (struct icmp6_echo_hdr *)p->payload;
    if ((ie6->id != lwip_htons(PING_ID)) || (lwip_ntohs(ie6->seqno) != ctx->seq) ||
        (ie6->type != ICMP6_TYPE_EREP)) {
      pbuf_add_header(p, IP6_HLEN);
      return 0;
    }
  }

  {
    uint64_t delta;

    delta = pm_metal_time_mono_us() - ctx->send_us;
    if (delta > 0xffffffffull) {
      delta = 0xffffffffull;
    }

    ctx->rtt_us = (uint32_t)delta;
  }
  ctx->ok = 1;
  pbuf_free(p);
  return 1;
}

static int32_t PingIsLoopbackNetif(const struct netif *n)
{
  return (n != NULL && n->name[0] == 'l' && n->name[1] == 'o') ? 1 : 0;
}

/** Prefer a non-loopback up iface so ICMP never exits via lo. */
static struct netif *PingOutNetif(const ip_addr_t *dst)
{
  struct netif *n;

  (void)dst;
  n = netif_default;
  if (n != NULL && !PingIsLoopbackNetif(n) && netif_is_up(n) && netif_is_link_up(n)) {
    return n;
  }

  NETIF_FOREACH(n)
  {
    if (PingIsLoopbackNetif(n) || !netif_is_up(n) || !netif_is_link_up(n)) {
      continue;
    }

#if LWIP_IPV4
    if (!ip4_addr_isany_val(*netif_ip4_addr(n))) {
      return n;
    }
#endif
  }

  return netif_default;
}

static err_t PingSend(ping_ctx_t *p)
{
  struct pbuf *pb;
  err_t        e;

  if (p == NULL || p->pcb == NULL) {
    return ERR_ARG;
  }

  p->seq = (uint16_t)((pm_metal_time_mono_us() >> 10) & 0xffffu);

  if (!p->target_v6) {
    struct icmp_echo_hdr *iecho;
    size_t                i;
    size_t                ping_size;

    ping_size = sizeof(struct icmp_echo_hdr) + PING_DATA_SIZE;
    pb        = pbuf_alloc(PBUF_IP, (uint16_t)ping_size, PBUF_RAM);
    if (pb == NULL) {
      return ERR_MEM;
    }

    iecho = (struct icmp_echo_hdr *)pb->payload;
    ICMPH_TYPE_SET(iecho, ICMP_ECHO);
    ICMPH_CODE_SET(iecho, 0);
    iecho->chksum = 0;
    iecho->id     = PING_ID;
    iecho->seqno  = lwip_htons(p->seq);
    for (i = 0; i < PING_DATA_SIZE; i++) {
      ((uint8_t *)iecho)[sizeof(struct icmp_echo_hdr) + i] = (uint8_t)i;
    }

    iecho->chksum = inet_chksum(iecho, (uint16_t)ping_size);
  } else {
    struct icmp6_echo_hdr *ie6;

    pb = pbuf_alloc(PBUF_IP, (uint16_t)(sizeof(struct icmp6_echo_hdr) + PING_DATA_SIZE), PBUF_RAM);
    if (pb == NULL) {
      return ERR_MEM;
    }

    ie6         = (struct icmp6_echo_hdr *)pb->payload;
    ie6->type   = ICMP6_TYPE_EREQ;
    ie6->code   = 0;
    ie6->chksum = 0;
    ie6->id     = lwip_htons(PING_ID);
    ie6->seqno  = lwip_htons(p->seq);
  }

  p->send_us = pm_metal_time_mono_us();
  e          = raw_sendto(p->pcb, pb, &p->target);
  pbuf_free(pb);
  return e;
}

static void PingClassifySendErr(err_t e)
{
  if (e == ERR_RTE) {
    mPingLastErr = PM_METAL_NET_PING_ERR_NOROUTE;
  } else if (e == ERR_MEM || e == ERR_BUF) {
    mPingLastErr = PM_METAL_NET_PING_ERR_NOMEM;
  } else {
    mPingLastErr = PM_METAL_NET_PING_ERR_SEND;
  }
}

static pm_metal_status_t PingStep(pm_metal_async_handle_t self_h)
{
  ping_ctx_t *p;

  p = (ping_ctx_t *)(uintptr_t)pm_metal_async_coro_state(self_h);
  if (p == NULL) {
    return PM_METAL_ERROR;
  }

  switch (p->step) {
  case PING_STEP_RESOLVE:
    p->ok         = 0;
    p->rtt_us     = 0;
    p->pcb        = NULL;
    p->seq        = 0;
    p->send_tries = 0;
    p->last_err   = ERR_OK;
    p->aw         = PM_METAL_ASYNC_HANDLE_INVALID;
    mPingLastErr  = PM_METAL_NET_PING_ERR_NONE;

    if (PingHostIsLiteral(p->host, &p->target, &p->target_v6) == 0) {
      p->step = PING_STEP_OPEN;
      return PM_METAL_PENDING;
    }

    /* Async DNS (literals/hosts/cache handled inside pm_metal_net_ip_dns). */
    p->aw = pm_metal_net_ip_dns(p->host);
    if (p->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      mPingLastErr = PM_METAL_NET_PING_ERR_RESOLVE;
      return PM_METAL_ERROR;
    }

    p->step = PING_STEP_DNS_AW;
    return pm_metal_async_await(self_h, p->aw);

  case PING_STEP_DNS_AW:
    if (pm_metal_async_result_u32(self_h) == 0) {
      mPingLastErr = PM_METAL_NET_PING_ERR_RESOLVE;
      return PM_METAL_ERROR;
    }

    {
      char ipstr[64];

      if (pm_metal_net_ip_dns_last_ntoa(ipstr, sizeof(ipstr)) != 0 ||
          PingHostIsLiteral(ipstr, &p->target, &p->target_v6) != 0) {
        mPingLastErr = PM_METAL_NET_PING_ERR_RESOLVE;
        return PM_METAL_ERROR;
      }
    }

    p->step = PING_STEP_OPEN;
    return PM_METAL_PENDING;

  case PING_STEP_OPEN: {
    struct netif *outif;
    u8_t          type;

    type   = p->target_v6 ? (u8_t)IPADDR_TYPE_V6 : (u8_t)IPADDR_TYPE_V4;
    p->pcb = raw_new_ip_type(type, PingRawProto(p));
    if (p->pcb == NULL) {
      mPingLastErr = PM_METAL_NET_PING_ERR_NOMEM;
      return PM_METAL_ERROR;
    }

    if (p->target_v6) {
      raw_bind(p->pcb, IP6_ADDR_ANY);
    } else {
      raw_bind(p->pcb, IP4_ADDR_ANY);
    }

    outif = PingOutNetif(&p->target);
    if (outif != NULL) {
      raw_bind_netif(p->pcb, outif);
    }

    raw_recv(p->pcb, PingRecv, p);
    p->send_tries = 0;
    p->last_err   = ERR_OK;
    p->step       = PING_STEP_SEND;
    return PM_METAL_PENDING;
  }

  case PING_STEP_SEND:
    pm_metal_net_ip_poll();
    p->last_err = PingSend(p);
    if (p->last_err == ERR_OK) {
      p->deadline = pm_metal_time_mono_us() + ((uint64_t)p->timeout_ms * 1000ull);
      p->step     = PING_STEP_WAIT;
      return PM_METAL_PENDING;
    }

    /* ARP / route often needs a few polls before the first frame exits. */
    if ((p->last_err == ERR_RTE || p->last_err == ERR_MEM || p->last_err == ERR_BUF ||
         p->last_err == ERR_IF) &&
        p->send_tries + 1u < PING_SEND_TRIES) {
      p->send_tries++;
      return pm_metal_async_await(self_h, pm_metal_async_sleep_us(25000));
    }

    PingCleanup(p);
    PingClassifySendErr(p->last_err);
    return PM_METAL_ERROR;

  case PING_STEP_WAIT:
    pm_metal_net_ip_poll();
    if (p->ok) {
      p->step = PING_STEP_DONE;
      return PM_METAL_PENDING;
    }

    if (pm_metal_time_mono_us() >= p->deadline) {
      PingCleanup(p);
      mPingLastErr = PM_METAL_NET_PING_ERR_TIMEOUT;
      return PM_METAL_ERROR;
    }

    /* Cooperative wait — same cadence as net DNS/connect coros. */
    return pm_metal_async_await(self_h, pm_metal_async_sleep_us(2000));

  case PING_STEP_DONE:
    PingCleanup(p);
    mPingLastDone.valid  = 1;
    mPingLastDone.rtt_us = p->rtt_us;
    mPingLastDone.err    = PM_METAL_NET_PING_ERR_NONE;
    mPingLastErr         = PM_METAL_NET_PING_ERR_NONE;
    /* result = ms (floored) for older callers; use rtt_us for tenths. */
    pm_metal_async_set_result_u32(self_h, p->rtt_us / 1000u);
    return PM_METAL_DONE;

  default:
    mPingLastErr = PM_METAL_NET_PING_ERR_SEND;
    return PM_METAL_ERROR;
  }
}

static void PingRelease(void *state)
{
  PingCleanup((ping_ctx_t *)state);
}

pm_metal_async_handle_t pm_metal_net_ping(const char *host, uint32_t timeout_ms)
{
  ping_ctx_t             *p;
  pm_metal_async_handle_t h;

  if (host == NULL || timeout_ms == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  h = pm_metal_async_coro_create(PingStep, sizeof(*p));
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  p = (ping_ctx_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (p == NULL) {
    pm_metal_async_coro_close(h);
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  mPingLastDone.valid = 0;
  mPingLastErr        = PM_METAL_NET_PING_ERR_NONE;
  p->step             = PING_STEP_RESOLVE;
  p->timeout_ms       = timeout_ms;
  snprintf(p->host, sizeof(p->host), "%s", host);
  pm_metal_async_coro_set_release(h, PingRelease);
  return h;
}

uint32_t pm_metal_net_ping_rtt_us(pm_metal_async_handle_t hnd)
{
  ping_ctx_t *p;

  p = (ping_ctx_t *)(uintptr_t)pm_metal_async_coro_state(hnd);
  if (p != NULL) {
    return p->rtt_us;
  }

  if (mPingLastDone.valid) {
    return mPingLastDone.rtt_us;
  }

  return 0;
}

uint32_t pm_metal_net_ping_rtt_ms(pm_metal_async_handle_t hnd)
{
  return pm_metal_net_ping_rtt_us(hnd) / 1000u;
}

uint32_t pm_metal_net_ping_last_err(void)
{
  if (mPingLastDone.valid && mPingLastDone.err != PM_METAL_NET_PING_ERR_NONE) {
    return mPingLastDone.err;
  }

  return mPingLastErr;
}

#if !defined(__wasm__)

#define PING_SHELL_TIMEOUT_MS 5000u

static void PingShellCmd(int32_t argc, char **argv)
{
  pm_metal_async_handle_t ping_h;
  pm_metal_async_handle_t task_h;
  uint64_t                deadline;

  if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
    pm_metal_shell_out("usage: ping <host>");
    return;
  }

  if (pm_metal_shell_job_busy()) {
    pm_metal_shell_out("ping: busy");
    return;
  }

  ping_h = pm_metal_net_ping(argv[1], PING_SHELL_TIMEOUT_MS);
  if (ping_h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_shell_out("ping: start failed");
    return;
  }

  task_h = pm_metal_async_create_task(ping_h);
  if (task_h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_async_coro_close(ping_h);
    pm_metal_shell_out("ping: task failed");
    return;
  }

  deadline = pm_metal_time_mono_us() + ((uint64_t)PING_SHELL_TIMEOUT_MS * 1000ull) + 500000ull;
  if (pm_metal_shell_job_start("ping", task_h, ping_h, argv[1], deadline) != 0) {
    pm_metal_async_task_cancel(task_h);
    pm_metal_shell_out("ping: job failed");
    return;
  }

  pm_metal_shell_out("ping: ...");
}

PM_METAL_SHELL_CMD(g_pm_metal_shell_cmd_ping,
                   "ping",
                   "ping <host>       ICMP echo (literal or DNS)",
                   PingShellCmd);

static int32_t PingGuestCopyHost(wasm_exec_env_t exec_env,
                                 const char     *host,
                                 char           *out,
                                 uintptr_t       out_sz)
{
  wasm_module_inst_t inst;
  uintptr_t          i;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || host == NULL || out == NULL || out_sz == 0) {
    return -1;
  }

  if (!wasm_runtime_validate_native_addr(inst, (void *)host, 1)) {
    return -1;
  }

  for (i = 0; i + 1 < out_sz; i++) {
    if (!wasm_runtime_validate_native_addr(inst, (void *)(host + i), 1)) {
      return -1;
    }

    out[i] = host[i];
    if (host[i] == '\0') {
      return 0;
    }
  }

  return -1;
}

static uint32_t pm_metal_net_ping_native(wasm_exec_env_t exec_env,
                                         const char     *host,
                                         uint32_t        timeout_ms)
{
  char cleaned[PING_HOST_MAX];

  if (PingGuestCopyHost(exec_env, host, cleaned, sizeof(cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_net_ping(cleaned, timeout_ms);
}

static uint32_t pm_metal_net_ping_rtt_ms_native(wasm_exec_env_t exec_env, uint32_t hnd)
{
  (void)exec_env;
  return pm_metal_net_ping_rtt_ms(hnd);
}

static uint32_t pm_metal_net_ping_rtt_us_native(wasm_exec_env_t exec_env, uint32_t hnd)
{
  (void)exec_env;
  return pm_metal_net_ping_rtt_us(hnd);
}

static uint32_t pm_metal_net_ping_last_err_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_net_ping_last_err();
}

static NativeSymbol g_pm_metal_net_ping_native_symbols[] = {
  { "pm_metal_net_ping", (void *)pm_metal_net_ping_native, "($i)i", NULL },
  { "pm_metal_net_ping_rtt_ms", (void *)pm_metal_net_ping_rtt_ms_native, "(i)i", NULL },
  { "pm_metal_net_ping_rtt_us", (void *)pm_metal_net_ping_rtt_us_native, "(i)i", NULL },
  { "pm_metal_net_ping_last_err", (void *)pm_metal_net_ping_last_err_native, "()i", NULL },
};

int pm_metal_net_ping_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_NET_PING_WASI_MODULE,
                                     g_pm_metal_net_ping_native_symbols,
                                     sizeof(g_pm_metal_net_ping_native_symbols) /
                                       sizeof(g_pm_metal_net_ping_native_symbols[0]))) {
    return -1;
  }

  return 0;
}

#endif
