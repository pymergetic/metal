/** @file
  ICMP echo (ping) — async coro over lwIP raw PCBs.
  (impl: efi|bios)
**/
#include <pymergetic/metal/dev/net/ping.h>
#include <pymergetic/metal/dev/net/net.h>
#include <pymergetic/metal/dev/net/net_cfg.h>
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <runtime/coro/coro.h>
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

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>

#include "wasm_export.h"

#include <stdint.h>

#define PING_HOST_MAX   128u
#define PING_ID         0x4d54u
#define PING_PROTO_V4  1u  /* IP_PROTO_ICMP */
#define PING_PROTO_V6  58u /* IPv6 next header: ICMPv6 */
#define PING_DATA_SIZE  32u

typedef enum {
  PING_STEP_RESOLVE = 0,
  PING_STEP_DNS_AW,
  PING_STEP_SEND,
  PING_STEP_WAIT,
  PING_STEP_DONE
} ping_step_t;

typedef struct {
  pm_metal_coro_t          coro;
  ping_step_t              step;
  pm_metal_async_handle_t  aw;
  CHAR8                    host[PING_HOST_MAX];
  UINT32                   timeout_ms;
  UINT64                   deadline;
  struct raw_pcb          *pcb;
  ip_addr_t                target;
  INT32                    target_v6;
  UINT16                   seq;
  UINT64                   send_us;
  UINT32                   rtt_ms;
  INT32                    ok;
} ping_ctx_t;

STATIC wasm_module_inst_t  mPingInst;

STATIC struct {
  INT32   valid;
  UINT32  rtt_ms;
} mPingLastDone;

STATIC INT32
PingParseIpv4 (
  CONST CHAR8  *s,
  ip4_addr_t   *out
  )
{
  UINT32        a;
  UINT32        b;
  UINT32        c;
  UINT32        d;
  UINT32        v;
  CONST CHAR8  *p;
  UINT32       *n;

  if (s == NULL || out == NULL) {
    return -1;
  }

  a = b = c = d = 0;
  p = s;
  n = &a;
  v = 0;
  for (;;) {
    if (*p >= '0' && *p <= '9') {
      v = v * 10u + (UINT32)(*p - '0');
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
      IP4_ADDR (out, a, b, c, d);
      return 0;
    }

    return -1;
  }
}

STATIC INT32
PingParseIpv6 (
  CONST CHAR8  *s,
  ip6_addr_t   *out
  )
{
  if (s == NULL || out == NULL) {
    return -1;
  }

  return ip6addr_aton (s, out) ? 0 : -1;
}

STATIC INT32
PingHostIsLiteral (
  CONST CHAR8  *host,
  ip_addr_t    *out,
  INT32        *v6
  )
{
  ip4_addr_t  a4;
  ip6_addr_t  a6;

  if (host == NULL || out == NULL || v6 == NULL) {
    return -1;
  }

  if (PingParseIpv4 (host, &a4) == 0) {
    ip_addr_copy_from_ip4 (*out, a4);
    *v6 = 0;
    return 0;
  }

  if (PingParseIpv6 (host, &a6) == 0) {
    ip_addr_copy_from_ip6 (*out, a6);
    *v6 = 1;
    return 0;
  }

  return -1;
}

STATIC UINT32
PingChildResult (
  pm_metal_coro_t  *self
  )
{
  return (UINT32)(UINTN)self->result;
}

STATIC u8_t
PingRawProto (
  ping_ctx_t  *p
  )
{
  return p->target_v6 ? PING_PROTO_V6 : PING_PROTO_V4;
}

STATIC VOID
PingCleanup (
  ping_ctx_t  *p
  )
{
  if (p == NULL) {
    return;
  }

  if (p->pcb != NULL) {
    raw_remove (p->pcb);
    p->pcb = NULL;
  }
}

STATIC u8_t
PingRecv (
  VOID           *arg,
  struct raw_pcb *pcb,
  struct pbuf    *p,
  CONST ip_addr_t *addr
  )
{
  ping_ctx_t             *ctx;
  struct icmp_echo_hdr   *iecho;
  struct icmp6_echo_hdr  *ie6;

  (VOID)pcb;
  (VOID)addr;
  ctx = (ping_ctx_t *)arg;
  if (ctx == NULL || p == NULL || ctx->ok) {
    return 0;
  }

  if (!ctx->target_v6) {
    CONST struct ip_hdr  *iphdr;
    u16_t                 hlen;

    iphdr = ip4_current_header ();
    if (iphdr == NULL) {
      return 0;
    }

    hlen = IPH_HL_BYTES (iphdr);
    if (p->tot_len < (hlen + sizeof (struct icmp_echo_hdr))) {
      return 0;
    }

    if (pbuf_remove_header (p, hlen) != 0) {
      return 0;
    }

    iecho = (struct icmp_echo_hdr *)p->payload;
    if ((iecho->id != PING_ID)
        || (iecho->seqno != lwip_htons (ctx->seq))
        || (ICMPH_TYPE (iecho) != ICMP_ER))
    {
      pbuf_add_header (p, hlen);
      return 0;
    }
  } else {
    if (p->tot_len < (IP6_HLEN + sizeof (struct icmp6_echo_hdr))) {
      return 0;
    }

    if (pbuf_remove_header (p, IP6_HLEN) != 0) {
      return 0;
    }

    ie6 = (struct icmp6_echo_hdr *)p->payload;
    if ((ie6->id != lwip_htons (PING_ID))
        || (lwip_ntohs (ie6->seqno) != ctx->seq)
        || (ie6->type != ICMP6_TYPE_EREP))
    {
      pbuf_add_header (p, IP6_HLEN);
      return 0;
    }
  }

  ctx->rtt_ms = (UINT32)((pm_metal_time_mono_us () - ctx->send_us) / 1000ULL);
  ctx->ok     = 1;
  pbuf_free (p);
  return 1;
}

STATIC INT32
PingSend (
  ping_ctx_t  *p
  )
{
  struct pbuf  *pb;
  err_t         e;

  if (p == NULL || p->pcb == NULL) {
    return -1;
  }

  p->seq = (UINT16)((pm_metal_time_mono_us () >> 10) & 0xffffu);

  if (!p->target_v6) {
    struct icmp_echo_hdr  *iecho;
    size_t                 i;
    size_t                 ping_size;

    ping_size = sizeof (struct icmp_echo_hdr) + PING_DATA_SIZE;
    pb        = pbuf_alloc (PBUF_IP, (UINT16)ping_size, PBUF_RAM);
    if (pb == NULL) {
      return -1;
    }

    iecho = (struct icmp_echo_hdr *)pb->payload;
    ICMPH_TYPE_SET (iecho, ICMP_ECHO);
    ICMPH_CODE_SET (iecho, 0);
    iecho->chksum = 0;
    iecho->id     = PING_ID;
    iecho->seqno  = lwip_htons (p->seq);
    for (i = 0; i < PING_DATA_SIZE; i++) {
      ((UINT8 *)iecho)[sizeof (struct icmp_echo_hdr) + i] = (UINT8)i;
    }

    iecho->chksum = inet_chksum (iecho, ping_size);
  } else {
    struct icmp6_echo_hdr  *ie6;

    pb = pbuf_alloc (PBUF_IP, sizeof (struct icmp6_echo_hdr) + PING_DATA_SIZE,
                     PBUF_RAM);
    if (pb == NULL) {
      return -1;
    }

    ie6 = (struct icmp6_echo_hdr *)pb->payload;
    ie6->type = ICMP6_TYPE_EREQ;
    ie6->code = 0;
    ie6->chksum = 0;
    ie6->id     = lwip_htons (PING_ID);
    ie6->seqno  = lwip_htons (p->seq);
  }

  p->send_us = pm_metal_time_mono_us ();
  e          = raw_sendto (p->pcb, pb, &p->target);
  pbuf_free (pb);
  return (e == ERR_OK) ? 0 : -1;
}

STATIC pm_metal_status_t
PingCoro (
  pm_metal_coro_t  *self
  )
{
  ping_ctx_t  *p;

  p = (ping_ctx_t *)self;

  switch (p->step) {
  case PING_STEP_RESOLVE:
    p->ok     = 0;
    p->rtt_ms = 0;
    p->pcb    = NULL;
    p->seq    = 0;
    p->aw     = PM_METAL_ASYNC_HANDLE_INVALID;

    if (PingHostIsLiteral (p->host, &p->target, &p->target_v6) == 0) {
      p->step = PING_STEP_SEND;
      return PM_METAL_PENDING;
    }

    /* Async DNS (literals/hosts/cache handled inside pm_metal_net_dns). */
    p->aw = pm_metal_net_dns (p->host);
    if (p->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      return PM_METAL_ERROR;
    }

    p->step = PING_STEP_DNS_AW;
    return (pm_metal_status_t)pm_metal_async_await_coro (self, p->aw);

  case PING_STEP_DNS_AW:
    if (PingChildResult (self) == 0) {
      return PM_METAL_ERROR;
    }

    {
      CHAR8  ipstr[64];

      if (pm_metal_net_dns_last_ntoa (ipstr, sizeof (ipstr)) != 0
          || PingHostIsLiteral (ipstr, &p->target, &p->target_v6) != 0)
      {
        return PM_METAL_ERROR;
      }
    }

    p->step = PING_STEP_SEND;
    return PM_METAL_PENDING;

  case PING_STEP_SEND:
    p->pcb = raw_new (PingRawProto (p));
    if (p->pcb == NULL) {
      return PM_METAL_ERROR;
    }

    raw_bind (p->pcb, IP_ADDR_ANY);
    raw_recv (p->pcb, PingRecv, p);

    if (PingSend (p) != 0) {
      PingCleanup (p);
      return PM_METAL_ERROR;
    }

    p->deadline = pm_metal_time_mono_us ()
                  + ((UINT64)p->timeout_ms * 1000ull);
    p->step = PING_STEP_WAIT;
    return PM_METAL_PENDING;

  case PING_STEP_WAIT:
    pm_metal_net_poll ();
    if (p->ok) {
      p->step = PING_STEP_DONE;
      return PM_METAL_PENDING;
    }

    if (pm_metal_time_mono_us () >= p->deadline) {
      PingCleanup (p);
      return PM_METAL_ERROR;
    }

    /* Cooperative wait — same cadence as net DNS/connect coros. */
    return pm_metal_await (self, pm_metal_sleep_us (2000));

  case PING_STEP_DONE:
    PingCleanup (p);
    mPingLastDone.valid  = 1;
    mPingLastDone.rtt_ms = p->rtt_ms;
    self->result         = (VOID *)(UINTN)p->rtt_ms;
    return PM_METAL_DONE;

  default:
    return PM_METAL_ERROR;
  }
}

STATIC VOID
PingRelease (
  pm_metal_coro_t  *self
  )
{
  PingCleanup ((ping_ctx_t *)self);
}

pm_metal_async_handle_t
pm_metal_net_ping (
  CONST CHAR8  *host,
  UINT32        timeout_ms
  )
{
  ping_ctx_t  *p;

  if (host == NULL || timeout_ms == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  p = (ping_ctx_t *)pm_metal_coro (PingCoro, sizeof (*p));
  if (p == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  mPingLastDone.valid = 0;
  p->coro.release     = PingRelease;
  p->step             = PING_STEP_RESOLVE;
  p->timeout_ms       = timeout_ms;
  AsciiStrCpyS (p->host, sizeof (p->host), host);

  return pm_metal_async_adopt_host_coro (&p->coro);
}

uint32_t
pm_metal_net_ping_rtt_ms (
  pm_metal_async_handle_t  hnd
  )
{
  ping_ctx_t  *p;

  p = (ping_ctx_t *)pm_metal_async_host_coro (hnd);
  if (p != NULL) {
    if (p->coro.status == PM_METAL_DONE && p->coro.result != NULL) {
      return (uint32_t)(UINTN)p->coro.result;
    }

    return p->rtt_ms;
  }

  if (mPingLastDone.valid) {
    return mPingLastDone.rtt_ms;
  }

  return 0;
}

#if !defined(__wasm__)

#define PING_SHELL_TIMEOUT_MS  5000u

STATIC
VOID
PingShellCmd (
  INT32   argc,
  CHAR8 **argv
  )
{
  pm_metal_async_handle_t  ping_h;
  pm_metal_async_handle_t  task_h;
  UINT64                   deadline;

  if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
    pm_metal_shell_out ("usage: ping <host>");
    return;
  }

  if (pm_metal_shell_job_busy ()) {
    pm_metal_shell_out ("ping: busy");
    return;
  }

  ping_h = pm_metal_net_ping (argv[1], PING_SHELL_TIMEOUT_MS);
  if (ping_h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_shell_out ("ping: start failed");
    return;
  }

  task_h = pm_metal_async_create_task (ping_h);
  if (task_h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_async_coro_close (ping_h);
    pm_metal_shell_out ("ping: task failed");
    return;
  }

  deadline = pm_metal_time_mono_us ()
             + ((UINT64)PING_SHELL_TIMEOUT_MS * 1000ull) + 500000ull;
  if (pm_metal_shell_job_start ("ping", task_h, ping_h, argv[1], deadline) != 0) {
    pm_metal_async_task_cancel (task_h);
    pm_metal_shell_out ("ping: job failed");
    return;
  }

  pm_metal_shell_out ("ping: …");
}

PM_METAL_SHELL_CMD (
  g_pm_metal_shell_cmd_ping,
  "ping",
  "ping <host>       ICMP echo (literal or DNS)",
  PingShellCmd
  );

STATIC INT32
PingGuestCopyHost (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *host,
  CHAR8           *out,
  UINTN            out_sz
  )
{
  wasm_module_inst_t  inst;
  UINTN               i;

  inst = wasm_runtime_get_module_inst (exec_env);
  if (inst == NULL || host == NULL || out == NULL || out_sz == 0) {
    return -1;
  }

  if (!wasm_runtime_validate_native_addr (inst, (VOID *)host, 1)) {
    return -1;
  }

  for (i = 0; i + 1 < out_sz; i++) {
    if (!wasm_runtime_validate_native_addr (inst, (VOID *)(host + i), 1)) {
      return -1;
    }

    out[i] = host[i];
    if (host[i] == '\0') {
      return 0;
    }
  }

  return -1;
}

STATIC UINT32
pm_metal_net_ping_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *host,
  UINT32           timeout_ms
  )
{
  CHAR8  cleaned[PING_HOST_MAX];

  if (mPingInst == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (PingGuestCopyHost (exec_env, host, cleaned, sizeof (cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_net_ping (cleaned, timeout_ms);
}

STATIC UINT32
pm_metal_net_ping_rtt_ms_native (
  wasm_exec_env_t  exec_env,
  UINT32           hnd
  )
{
  (VOID)exec_env;
  return pm_metal_net_ping_rtt_ms (hnd);
}

STATIC NativeSymbol g_pm_metal_net_ping_native_symbols[] = {
  { "pm_metal_net_ping", (VOID *)pm_metal_net_ping_native, "($i)i", NULL },
  { "pm_metal_net_ping_rtt_ms", (VOID *)pm_metal_net_ping_rtt_ms_native, "(i)i", NULL },
};

int
pm_metal_net_ping_native_register (
  VOID
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_NET_PING_WASI_MODULE,
         g_pm_metal_net_ping_native_symbols,
         sizeof (g_pm_metal_net_ping_native_symbols)
           / sizeof (g_pm_metal_net_ping_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}

void
pm_metal_net_ping_bind_inst (
  VOID  *module_inst
  )
{
  mPingInst = (wasm_module_inst_t)module_inst;
}

#endif
