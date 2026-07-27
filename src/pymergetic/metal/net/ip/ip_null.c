/** @file
  Null net backend — ABI-complete fail path. (impl: efi|bios)
**/
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/runtime/async/async.h>

#include <stdint.h>
#include <string.h>

#ifndef PM_METAL_NET_MAX_SOCKS
#define PM_METAL_NET_MAX_SOCKS 16u
#endif

typedef struct {
  int32_t  used;
  uint32_t domain;
  uint32_t type;
} null_sock_t;

static null_sock_t mSocks[PM_METAL_NET_MAX_SOCKS + 1];
static int32_t     mLogged;

static pm_metal_status_t NullFailStep(pm_metal_async_handle_t self_h)
{
  (void)self_h;
  return PM_METAL_ERROR;
}

static pm_metal_async_handle_t NullFailAsync(void)
{
  return pm_metal_async_coro_create(NullFailStep, 0);
}

static int NullInit(void)
{
  mLogged = 1;
  return 0;
}

static void NullPoll(void) {}

static pm_metal_net_sock_h NullSocket(uint32_t domain, uint32_t type)
{
  uint32_t i;

  if (domain == 0 || type == 0) {
    return PM_METAL_NET_SOCK_INVALID;
  }

  for (i = 1; i <= PM_METAL_NET_MAX_SOCKS; i++) {
    if (!mSocks[i].used) {
      mSocks[i].used   = 1;
      mSocks[i].domain = domain;
      mSocks[i].type   = type;
      return (pm_metal_net_sock_h)i;
    }
  }

  return PM_METAL_NET_SOCK_INVALID;
}

static void NullClose(pm_metal_net_sock_h h)
{
  if (h == 0 || h > PM_METAL_NET_MAX_SOCKS) {
    return;
  }

  memset(&mSocks[h], 0, sizeof(mSocks[h]));
}

static pm_metal_async_handle_t NullConnect(pm_metal_net_sock_h h, const char *host, uint32_t port)
{
  (void)host;
  (void)port;
  if (h == 0 || h > PM_METAL_NET_MAX_SOCKS || !mSocks[h].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return NullFailAsync();
}

static pm_metal_async_handle_t NullListen(pm_metal_net_sock_h h, uint32_t port)
{
  (void)port;
  if (h == 0 || !mSocks[h].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return NullFailAsync();
}

static pm_metal_async_handle_t NullAccept(pm_metal_net_sock_h h)
{
  if (h == 0 || !mSocks[h].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return NullFailAsync();
}

static uint32_t NullSend(pm_metal_net_sock_h h, const void *ptr, uint32_t len)
{
  (void)ptr;
  (void)len;
  (void)h;
  return 0;
}

static pm_metal_async_handle_t NullRecv(pm_metal_net_sock_h h, void *ptr, uint32_t len)
{
  (void)ptr;
  (void)len;
  if (h == 0 || !mSocks[h].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return NullFailAsync();
}

static pm_metal_async_handle_t NullDns(const char *host)
{
  (void)host;
  return NullFailAsync();
}

static int NullBindIf(pm_metal_net_sock_h h, const char *ifname)
{
  (void)h;
  (void)ifname;
  return -1;
}

static int NullBind(pm_metal_net_sock_h h, uint32_t port)
{
  (void)h;
  (void)port;
  return -1;
}

static uint32_t NullSendto(pm_metal_net_sock_h h, const void *ptr, uint32_t len, const char *host,
                           uint32_t port)
{
  (void)h;
  (void)ptr;
  (void)len;
  (void)host;
  (void)port;
  return 0;
}

static uint32_t NullTryRecvfrom(pm_metal_net_sock_h h, void *ptr, uint32_t len, char *peer_host,
                                uint32_t peer_cap, uint32_t *peer_port)
{
  (void)h;
  (void)ptr;
  (void)len;
  (void)peer_host;
  (void)peer_cap;
  (void)peer_port;
  return 0;
}

static const pm_metal_net_ops_t mNullOps = {
  "null",    NullInit,   NullPoll,  NullSocket, NullClose, NullConnect, NullListen, NullAccept,
  NullSend,  NullRecv,   NullDns,   NullBindIf, NULL,      NullBind,    NullSendto, NullTryRecvfrom
};

void pm_metal_net_null_install(void)
{
  pm_metal_net_set_ops(&mNullOps);
  (void)NullInit();
}
