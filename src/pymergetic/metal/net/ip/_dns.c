#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/coro.h>
#include <pymergetic/metal/async/await.h>
#include <pymergetic/metal/async/time.h>
#include <pymergetic/metal/net/ip/__init__.h>

#include "lwipopts.h" /* IWYU pragma: keep */
#include <lwip/dns.h>
#include <lwip/ip_addr.h>
#include <lwip/ip4_addr.h>

#define DNS_QUERY_SLOTS 4u
#define DNS_DEADLINE_US 8000000ull

typedef struct {
  int32_t   used;
  int32_t   done;
  int32_t   ok;
  int32_t   abandoned;
  ip_addr_t addr;
} dns_query_t;

typedef struct {
  dns_query_t *query;
  uint32_t     sleep_h;
  uint64_t     deadline;
} dns_coro_t;

static dns_query_t mDnsQueries[DNS_QUERY_SLOTS];
static ip4_addr_t  mDnsLast;
static int32_t     mDnsLastValid;

static dns_query_t *DnsQueryAlloc(void)
{
  uint32_t i;

  for (i = 0; i < DNS_QUERY_SLOTS; i++) {
    if (!mDnsQueries[i].used) {
      mDnsQueries[i].used      = 1;
      mDnsQueries[i].done      = 0;
      mDnsQueries[i].ok        = 0;
      mDnsQueries[i].abandoned = 0;
      return &mDnsQueries[i];
    }
  }
  return NULL;
}

static void DnsFound(const char *name, const ip_addr_t *addr, void *arg)
{
  dns_query_t *query;

  (void)name;
  query = (dns_query_t *)arg;
  if (query == NULL || !query->used) {
    return;
  }
  query->done = 1;
  query->ok   = addr != NULL && IP_IS_V4(addr);
  if (query->ok) {
    query->addr = *addr;
  }
  if (query->abandoned) {
    query->used = 0;
  }
}

static uint32_t DnsStartSleep(uint32_t self_h, dns_coro_t *coro)
{
  coro->sleep_h = pm_metal_async_sleep_us(2000);
  if (coro->sleep_h == 0) {
    return PM_METAL_ASYNC_ERROR;
  }
  return (uint32_t)pm_metal_async_await(self_h, coro->sleep_h);
}

static uint32_t DnsStep(uint32_t self_h)
{
  dns_coro_t             *coro;
  pm_metal_async_status_t status;

  coro = (dns_coro_t *)pm_metal_async_coro_state(self_h);
  if (coro == NULL || coro->query == NULL) {
    return PM_METAL_ASYNC_ERROR;
  }

  if (coro->sleep_h != 0) {
    status = pm_metal_async_await(self_h, coro->sleep_h);
    if (status == PM_METAL_ASYNC_WAITING) {
      return PM_METAL_ASYNC_WAITING;
    }
    pm_metal_async_coro_close(coro->sleep_h);
    coro->sleep_h = 0;
    if (status != PM_METAL_ASYNC_DONE) {
      return PM_METAL_ASYNC_ERROR;
    }
  }

  pm_metal_net_ip_poll();
  if (coro->query->done) {
    if (coro->query->ok) {
      mDnsLast          = *ip_2_ip4(&coro->query->addr);
      mDnsLastValid     = 1;
      coro->query->used = 0;
      pm_metal_async_set_result_u32(self_h, 1);
      return PM_METAL_ASYNC_DONE;
    }
    coro->query->used = 0;
    pm_metal_async_set_result_u32(self_h, 0);
    return PM_METAL_ASYNC_ERROR;
  }

  if (pm_metal_time_mono_us() >= coro->deadline) {
    coro->query->abandoned = 1;
    if (coro->query->done) {
      coro->query->used = 0;
    }
    pm_metal_async_set_result_u32(self_h, 0);
    return PM_METAL_ASYNC_ERROR;
  }

  return DnsStartSleep(self_h, coro);
}

uint32_t pm_metal_net_ip_dns(const char *host)
{
  dns_coro_t  *coro;
  dns_query_t *query;
  ip4_addr_t   literal;
  ip_addr_t    addr;
  uint32_t     h;
  err_t        err;

  if (host == NULL || host[0] == '\0') {
    return 0;
  }
  query = DnsQueryAlloc();
  if (query == NULL) {
    return 0;
  }

  h = pm_metal_async_coro_create(DnsStep, sizeof(*coro));
  if (h == 0) {
    query->used = 0;
    return 0;
  }
  coro = (dns_coro_t *)pm_metal_async_coro_state(h);
  if (coro == NULL) {
    query->used = 0;
    pm_metal_async_coro_close(h);
    return 0;
  }

  coro->query    = query;
  coro->deadline = pm_metal_time_mono_us() + DNS_DEADLINE_US;
  mDnsLastValid  = 0;

  if (ip4addr_aton(host, &literal)) {
    ip_addr_copy_from_ip4(query->addr, literal);
    query->done = 1;
    query->ok   = 1;
    return h;
  }

  err = dns_gethostbyname_addrtype(host, &addr, DnsFound, query, LWIP_DNS_ADDRTYPE_IPV4);
  if (err == ERR_OK) {
    query->addr = addr;
    query->done = 1;
    query->ok   = IP_IS_V4(&addr);
  } else if (err != ERR_INPROGRESS) {
    query->used = 0;
    pm_metal_async_coro_close(h);
    return 0;
  }
  return h;
}

int32_t pm_metal_net_ip_dns_last_ntoa(char *out, uint32_t out_cap)
{
  if (out == NULL || out_cap == 0 || !mDnsLastValid) {
    return -1;
  }
  return ip4addr_ntoa_r(&mDnsLast, out, (int32_t)out_cap) != NULL ? 0 : -1;
}
