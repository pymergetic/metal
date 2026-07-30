#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/coro.h>
#include <pymergetic/metal/async/await.h>
#include <pymergetic/metal/async/time.h>
#include <pymergetic/metal/net/ip/__init__.h>
#include <pymergetic/metal/net/ntp/__init__.h>

#include "lwipopts.h" /* IWYU pragma: keep */
#include <lwip/ip4_addr.h>
#include <lwip/ip_addr.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>

#define NTP_HOST_MAX    128u
#define NTP_PACKET_LEN  48u
#ifndef PM_METAL_NET_NTP_PORT
#define PM_METAL_NET_NTP_PORT 123u
#endif
#define NTP_PORT        PM_METAL_NET_NTP_PORT
#define NTP_TIMEOUT_US  3000000ull
#define NTP_RETRIES     3u
#define NTP_UNIX_OFFSET 2208988800ull

typedef enum {
  NTP_RESOLVE = 0,
  NTP_DNS_WAIT,
  NTP_OPEN,
  NTP_SEND,
  NTP_REPLY_WAIT
} ntp_phase_t;

typedef struct {
  ntp_phase_t     phase;
  char            host[NTP_HOST_MAX];
  uint32_t        child_h;
  uint32_t        status;
  uint32_t        retries;
  uint64_t        deadline;
  uint64_t        unix_ms;
  ip_addr_t       server;
  struct udp_pcb *pcb;
  int32_t         received;
  uint8_t         packet[NTP_PACKET_LEN];
} ntp_coro_t;

static struct {
  int32_t  valid;
  uint32_t status;
  uint64_t unix_ms;
} mNtpLast;

static void NtpCleanup(ntp_coro_t *ntp)
{
  if (ntp->pcb != NULL) {
    udp_remove(ntp->pcb);
    ntp->pcb = NULL;
  }
}

static uint32_t NtpFinish(ntp_coro_t *ntp, uint32_t self_h, uint32_t status)
{
  NtpCleanup(ntp);
  ntp->status      = status;
  mNtpLast.valid   = 1;
  mNtpLast.status  = status;
  mNtpLast.unix_ms = ntp->unix_ms;
  pm_metal_async_set_result_u32(self_h, status == 0 ? 1u : 0u);
  return status == 0 ? PM_METAL_ASYNC_DONE : PM_METAL_ASYNC_ERROR;
}

static void NtpRecv(
  void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, uint16_t port)
{
  ntp_coro_t *ntp;

  (void)pcb;
  ntp = (ntp_coro_t *)arg;
  if (ntp == NULL || p == NULL) {
    if (p != NULL) {
      pbuf_free(p);
    }
    return;
  }
  if (!ntp->received && port == NTP_PORT && ip_addr_cmp(addr, &ntp->server) &&
      p->tot_len >= NTP_PACKET_LEN &&
      pbuf_copy_partial(p, ntp->packet, NTP_PACKET_LEN, 0) == NTP_PACKET_LEN) {
    ntp->received = 1;
  }
  pbuf_free(p);
}

static uint32_t NtpReadBe32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int32_t NtpParse(ntp_coro_t *ntp)
{
  uint32_t sec;
  uint32_t frac;
  uint8_t  mode;

  mode = ntp->packet[0] & 7u;
  if ((ntp->packet[0] >> 6) == 3u || ntp->packet[1] == 0 || (mode != 4u && mode != 5u)) {
    return -1;
  }
  sec  = NtpReadBe32(ntp->packet + 40);
  frac = NtpReadBe32(ntp->packet + 44);
  if ((uint64_t)sec < NTP_UNIX_OFFSET) {
    return -1;
  }
  ntp->unix_ms =
    ((uint64_t)sec - NTP_UNIX_OFFSET) * 1000ull + ((uint64_t)frac * 1000ull) / 4294967296ull;
  return 0;
}

static int32_t NtpSend(ntp_coro_t *ntp)
{
  struct pbuf *p;
  uint8_t     *packet;
  err_t        err;

  p = pbuf_alloc(PBUF_TRANSPORT, NTP_PACKET_LEN, PBUF_RAM);
  if (p == NULL) {
    return -1;
  }
  packet = (uint8_t *)p->payload;
  memset(packet, 0, NTP_PACKET_LEN);
  packet[0] = 0x23u;
  err       = udp_sendto(ntp->pcb, p, &ntp->server, NTP_PORT);
  pbuf_free(p);
  return err == ERR_OK ? 0 : -1;
}

static int32_t NtpFinishChild(uint32_t self_h, ntp_coro_t *ntp, uint32_t *result)
{
  pm_metal_async_status_t status;

  status = pm_metal_async_await(self_h, ntp->child_h);
  if (status == PM_METAL_ASYNC_WAITING) {
    return 0;
  }
  if (result != NULL) {
    *result = pm_metal_async_result_u32(ntp->child_h);
  }
  pm_metal_async_coro_close(ntp->child_h);
  ntp->child_h = 0;
  return status == PM_METAL_ASYNC_DONE ? 1 : -1;
}

static uint32_t NtpStartSleep(uint32_t self_h, ntp_coro_t *ntp)
{
  ntp->child_h = pm_metal_async_sleep_us(2000);
  if (ntp->child_h == 0) {
    return NtpFinish(ntp, self_h, 4);
  }
  return (uint32_t)pm_metal_async_await(self_h, ntp->child_h);
}

static uint32_t NtpStep(uint32_t self_h)
{
  ntp_coro_t *ntp;
  int32_t     child;

  ntp = (ntp_coro_t *)pm_metal_async_coro_state(self_h);
  if (ntp == NULL) {
    return PM_METAL_ASYNC_ERROR;
  }

  switch (ntp->phase) {
  case NTP_RESOLVE: {
    ip4_addr_t literal;

    if (ip4addr_aton(ntp->host, &literal)) {
      ip_addr_copy_from_ip4(ntp->server, literal);
      ntp->phase = NTP_OPEN;
      return PM_METAL_ASYNC_PENDING;
    }
    ntp->child_h = pm_metal_net_ip_dns(ntp->host);
    if (ntp->child_h == 0) {
      return NtpFinish(ntp, self_h, 3);
    }
    ntp->phase = NTP_DNS_WAIT;
    return (uint32_t)pm_metal_async_await(self_h, ntp->child_h);
  }

  case NTP_DNS_WAIT: {
    uint32_t   ok;
    char       text[16];
    ip4_addr_t addr;

    child = NtpFinishChild(self_h, ntp, &ok);
    if (child == 0) {
      return PM_METAL_ASYNC_WAITING;
    }
    if (child < 0 || ok == 0 || pm_metal_net_ip_dns_last_ntoa(text, sizeof(text)) != 0 ||
        !ip4addr_aton(text, &addr)) {
      return NtpFinish(ntp, self_h, 3);
    }
    ip_addr_copy_from_ip4(ntp->server, addr);
    ntp->phase = NTP_OPEN;
    return PM_METAL_ASYNC_PENDING;
  }

  case NTP_OPEN:
    ntp->pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (ntp->pcb == NULL || udp_bind(ntp->pcb, IP4_ADDR_ANY, 0) != ERR_OK) {
      return NtpFinish(ntp, self_h, 4);
    }
    udp_recv(ntp->pcb, NtpRecv, ntp);
    ntp->phase = NTP_SEND;
    return PM_METAL_ASYNC_PENDING;

  case NTP_SEND:
    ntp->received = 0;
    if (NtpSend(ntp) != 0) {
      return NtpFinish(ntp, self_h, 5);
    }
    ntp->deadline = pm_metal_time_mono_us() + NTP_TIMEOUT_US;
    ntp->phase    = NTP_REPLY_WAIT;
    return PM_METAL_ASYNC_PENDING;

  case NTP_REPLY_WAIT:
    if (ntp->child_h != 0) {
      child = NtpFinishChild(self_h, ntp, NULL);
      if (child == 0) {
        return PM_METAL_ASYNC_WAITING;
      }
      if (child < 0) {
        return NtpFinish(ntp, self_h, 4);
      }
    }
    pm_metal_net_ip_poll();
    if (ntp->received) {
      if (NtpParse(ntp) != 0) {
        return NtpFinish(ntp, self_h, 6);
      }
      return NtpFinish(ntp, self_h, 0);
    }
    if (pm_metal_time_mono_us() >= ntp->deadline) {
      if (++ntp->retries > NTP_RETRIES) {
        return NtpFinish(ntp, self_h, 7);
      }
      ntp->phase = NTP_SEND;
      return PM_METAL_ASYNC_PENDING;
    }
    return NtpStartSleep(self_h, ntp);
  }

  return NtpFinish(ntp, self_h, 4);
}

uint32_t pm_metal_net_ntp_sync(const char *host)
{
  ntp_coro_t *ntp;
  uint32_t    h;

  if (host == NULL || host[0] == '\0' || strlen(host) >= NTP_HOST_MAX) {
    return 0;
  }
  h = pm_metal_async_coro_create(NtpStep, sizeof(*ntp));
  if (h == 0) {
    return 0;
  }
  ntp = (ntp_coro_t *)pm_metal_async_coro_state(h);
  if (ntp == NULL) {
    pm_metal_async_coro_close(h);
    return 0;
  }
  ntp->phase  = NTP_RESOLVE;
  ntp->status = 1;
  snprintf(ntp->host, sizeof(ntp->host), "%s", host);
  mNtpLast.valid = 0;
  return h;
}

uint32_t pm_metal_net_ntp_status(uint32_t h)
{
  ntp_coro_t *ntp;

  ntp = (ntp_coro_t *)pm_metal_async_coro_state(h);
  if (ntp != NULL) {
    return ntp->status;
  }
  return mNtpLast.valid ? mNtpLast.status : 1u;
}

uint64_t pm_metal_net_ntp_last_unix_ms(void)
{
  return mNtpLast.valid && mNtpLast.status == 0 ? mNtpLast.unix_ms : 0;
}
