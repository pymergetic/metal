#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/coro.h>
#include <pymergetic/metal/async/await.h>
#include <pymergetic/metal/async/time.h>
#include <pymergetic/metal/net/ip/__init__.h>
#include <pymergetic/metal/net/ping/__init__.h>

#include "lwipopts.h" /* IWYU pragma: keep */
#include <lwip/err.h>
#include <lwip/icmp.h>
#include <lwip/inet_chksum.h>
#include <lwip/ip4.h>
#include <lwip/ip4_addr.h>
#include <lwip/ip_addr.h>
#include <lwip/pbuf.h>
#include <lwip/prot/ip4.h>
#include <lwip/raw.h>

#define PING_HOST_MAX   128u
#define PING_ID         0x4d54u
#define PING_DATA_SIZE  32u
#define PING_SEND_TRIES 40u

typedef enum {
  PING_RESOLVE = 0,
  PING_DNS_WAIT,
  PING_OPEN,
  PING_SEND,
  PING_SEND_SLEEP,
  PING_REPLY_WAIT
} ping_phase_t;

typedef struct {
  ping_phase_t    phase;
  char            host[PING_HOST_MAX];
  uint32_t        timeout_ms;
  uint32_t        child_h;
  uint32_t        send_tries;
  struct raw_pcb *pcb;
  ip_addr_t       target;
  uint16_t        seq;
  uint64_t        send_us;
  uint64_t        deadline;
  uint32_t        rtt_us;
  int32_t         reply;
} ping_coro_t;

static struct {
  int32_t  valid;
  uint32_t rtt_us;
} mPingLast;
static uint32_t mPingLastErr;

static void PingCleanup(ping_coro_t *ping)
{
  if (ping->pcb != NULL) {
    raw_remove(ping->pcb);
    ping->pcb = NULL;
  }
}

static uint32_t PingFail(ping_coro_t *ping, uint32_t err)
{
  PingCleanup(ping);
  mPingLastErr = err;
  return PM_METAL_ASYNC_ERROR;
}

static uint8_t PingRecv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
  ping_coro_t          *ping;
  struct icmp_echo_hdr *echo;
  const struct ip_hdr  *ip;
  uint16_t              hlen;
  uint64_t              elapsed;

  (void)pcb;
  (void)addr;
  ping = (ping_coro_t *)arg;
  if (ping == NULL || p == NULL || ping->reply) {
    return 0;
  }

  ip = ip4_current_header();
  if (ip == NULL) {
    return 0;
  }
  hlen = IPH_HL_BYTES(ip);
  if (p->tot_len < hlen + sizeof(*echo) || pbuf_remove_header(p, hlen) != 0) {
    return 0;
  }

  echo = (struct icmp_echo_hdr *)p->payload;
  if (ICMPH_TYPE(echo) != ICMP_ER || echo->id != PING_ID || echo->seqno != lwip_htons(ping->seq)) {
    pbuf_add_header(p, hlen);
    return 0;
  }

  elapsed      = pm_metal_time_mono_us() - ping->send_us;
  ping->rtt_us = elapsed > 0xffffffffull ? 0xffffffffu : (uint32_t)elapsed;
  ping->reply  = 1;
  pbuf_free(p);
  return 1;
}

static err_t PingSendEcho(ping_coro_t *ping)
{
  struct pbuf          *p;
  struct icmp_echo_hdr *echo;
  uint32_t              i;
  uint32_t              len;
  err_t                 err;

  len = (uint32_t)sizeof(*echo) + PING_DATA_SIZE;
  p   = pbuf_alloc(PBUF_IP, (uint16_t)len, PBUF_RAM);
  if (p == NULL) {
    return ERR_MEM;
  }
  echo = (struct icmp_echo_hdr *)p->payload;
  ICMPH_TYPE_SET(echo, ICMP_ECHO);
  ICMPH_CODE_SET(echo, 0);
  echo->chksum = 0;
  echo->id     = PING_ID;
  echo->seqno  = lwip_htons(ping->seq);
  for (i = 0; i < PING_DATA_SIZE; i++) {
    ((uint8_t *)echo)[sizeof(*echo) + i] = (uint8_t)i;
  }
  echo->chksum  = inet_chksum(echo, (uint16_t)len);
  ping->send_us = pm_metal_time_mono_us();
  err           = raw_sendto(ping->pcb, p, &ping->target);
  pbuf_free(p);
  return err;
}

static int32_t PingFinishChild(uint32_t self_h, ping_coro_t *ping, uint32_t *result)
{
  pm_metal_async_status_t status;

  status = pm_metal_async_await(self_h, ping->child_h);
  if (status == PM_METAL_ASYNC_WAITING) {
    return 0;
  }
  if (result != NULL) {
    *result = pm_metal_async_result_u32(ping->child_h);
  }
  pm_metal_async_coro_close(ping->child_h);
  ping->child_h = 0;
  return status == PM_METAL_ASYNC_DONE ? 1 : -1;
}

static uint32_t PingStartSleep(uint32_t self_h, ping_coro_t *ping, uint64_t us)
{
  ping->child_h = pm_metal_async_sleep_us(us);
  if (ping->child_h == 0) {
    return PingFail(ping, PM_METAL_NET_PING_ERR_SEND);
  }
  return (uint32_t)pm_metal_async_await(self_h, ping->child_h);
}

static uint32_t PingStep(uint32_t self_h)
{
  ping_coro_t *ping;
  int32_t      child;

  ping = (ping_coro_t *)pm_metal_async_coro_state(self_h);
  if (ping == NULL) {
    return PM_METAL_ASYNC_ERROR;
  }

  switch (ping->phase) {
  case PING_RESOLVE: {
    ip4_addr_t literal;

    if (ip4addr_aton(ping->host, &literal)) {
      ip_addr_copy_from_ip4(ping->target, literal);
      ping->phase = PING_OPEN;
      return PM_METAL_ASYNC_PENDING;
    }
    ping->child_h = pm_metal_net_ip_dns(ping->host);
    if (ping->child_h == 0) {
      return PingFail(ping, PM_METAL_NET_PING_ERR_RESOLVE);
    }
    ping->phase = PING_DNS_WAIT;
    return (uint32_t)pm_metal_async_await(self_h, ping->child_h);
  }

  case PING_DNS_WAIT: {
    uint32_t   ok;
    char       text[16];
    ip4_addr_t addr;

    child = PingFinishChild(self_h, ping, &ok);
    if (child == 0) {
      return PM_METAL_ASYNC_WAITING;
    }
    if (child < 0 || ok == 0 || pm_metal_net_ip_dns_last_ntoa(text, sizeof(text)) != 0 ||
        !ip4addr_aton(text, &addr)) {
      return PingFail(ping, PM_METAL_NET_PING_ERR_RESOLVE);
    }
    ip_addr_copy_from_ip4(ping->target, addr);
    ping->phase = PING_OPEN;
    return PM_METAL_ASYNC_PENDING;
  }

  case PING_OPEN:
    ping->pcb = raw_new(IP_PROTO_ICMP);
    if (ping->pcb == NULL) {
      return PingFail(ping, PM_METAL_NET_PING_ERR_NOMEM);
    }
    raw_recv(ping->pcb, PingRecv, ping);
    ping->seq   = (uint16_t)((pm_metal_time_mono_us() >> 10) & 0xffffu);
    ping->phase = PING_SEND;
    return PM_METAL_ASYNC_PENDING;

  case PING_SEND: {
    err_t err;

    pm_metal_net_ip_poll();
    err = PingSendEcho(ping);
    if (err == ERR_OK) {
      ping->deadline = pm_metal_time_mono_us() + (uint64_t)ping->timeout_ms * 1000ull;
      ping->phase    = PING_REPLY_WAIT;
      return PM_METAL_ASYNC_PENDING;
    }
    if ((err == ERR_RTE || err == ERR_IF || err == ERR_MEM || err == ERR_BUF) &&
        ping->send_tries++ < PING_SEND_TRIES) {
      ping->phase = PING_SEND_SLEEP;
      return PingStartSleep(self_h, ping, 25000);
    }
    if (err == ERR_RTE) {
      return PingFail(ping, PM_METAL_NET_PING_ERR_NOROUTE);
    }
    if (err == ERR_MEM || err == ERR_BUF) {
      return PingFail(ping, PM_METAL_NET_PING_ERR_NOMEM);
    }
    return PingFail(ping, PM_METAL_NET_PING_ERR_SEND);
  }

  case PING_SEND_SLEEP:
    child = PingFinishChild(self_h, ping, NULL);
    if (child == 0) {
      return PM_METAL_ASYNC_WAITING;
    }
    if (child < 0) {
      return PingFail(ping, PM_METAL_NET_PING_ERR_SEND);
    }
    ping->phase = PING_SEND;
    return PM_METAL_ASYNC_PENDING;

  case PING_REPLY_WAIT:
    if (ping->child_h != 0) {
      child = PingFinishChild(self_h, ping, NULL);
      if (child == 0) {
        return PM_METAL_ASYNC_WAITING;
      }
      if (child < 0) {
        return PingFail(ping, PM_METAL_NET_PING_ERR_SEND);
      }
    }
    pm_metal_net_ip_poll();
    if (ping->reply) {
      PingCleanup(ping);
      mPingLast.valid  = 1;
      mPingLast.rtt_us = ping->rtt_us;
      mPingLastErr     = PM_METAL_NET_PING_ERR_NONE;
      pm_metal_async_set_result_u32(self_h, ping->rtt_us / 1000u);
      return PM_METAL_ASYNC_DONE;
    }
    if (pm_metal_time_mono_us() >= ping->deadline) {
      return PingFail(ping, PM_METAL_NET_PING_ERR_TIMEOUT);
    }
    return PingStartSleep(self_h, ping, 2000);
  }

  return PingFail(ping, PM_METAL_NET_PING_ERR_SEND);
}

uint32_t pm_metal_net_ping(const char *host, uint32_t timeout_ms)
{
  ping_coro_t *ping;
  uint32_t     h;

  if (host == NULL || host[0] == '\0' || timeout_ms == 0 || strlen(host) >= PING_HOST_MAX) {
    return 0;
  }
  h = pm_metal_async_coro_create(PingStep, sizeof(*ping));
  if (h == 0) {
    return 0;
  }
  ping = (ping_coro_t *)pm_metal_async_coro_state(h);
  if (ping == NULL) {
    pm_metal_async_coro_close(h);
    return 0;
  }
  ping->phase      = PING_RESOLVE;
  ping->timeout_ms = timeout_ms;
  snprintf(ping->host, sizeof(ping->host), "%s", host);
  mPingLast.valid = 0;
  mPingLastErr    = PM_METAL_NET_PING_ERR_NONE;
  return h;
}

uint32_t pm_metal_net_ping_rtt_ms(uint32_t h)
{
  return pm_metal_net_ping_rtt_us(h) / 1000u;
}

uint32_t pm_metal_net_ping_rtt_us(uint32_t h)
{
  ping_coro_t *ping;

  ping = (ping_coro_t *)pm_metal_async_coro_state(h);
  if (ping != NULL) {
    return ping->rtt_us;
  }
  return mPingLast.valid ? mPingLast.rtt_us : 0;
}

uint32_t pm_metal_net_ping_last_err(void)
{
  return mPingLastErr;
}
