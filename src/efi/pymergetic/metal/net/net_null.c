/** @file
  Null net backend — ABI-complete fail path. (impl: efi)
**/
#include <pymergetic/metal/net/net_ops.h>
#include <pymergetic/metal/async/async.h>
#include <coro/coro.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>

#ifndef PM_METAL_NET_MAX_SOCKS
#define PM_METAL_NET_MAX_SOCKS  16u
#endif

typedef struct {
  INT32   used;
  UINT32  domain;
  UINT32  type;
} null_sock_t;

STATIC null_sock_t  mSocks[PM_METAL_NET_MAX_SOCKS + 1];
STATIC INT32        mLogged;

STATIC
pm_metal_status_t
NullFailFn (
  pm_metal_coro_t  *self
  )
{
  (VOID)self;
  return PM_METAL_ERROR;
}

STATIC
pm_metal_async_handle_t
NullFailAsync (
  VOID
  )
{
  pm_metal_coro_t  *c;

  c = pm_metal_coro (NullFailFn, sizeof (*c));
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_async_adopt_host_coro (c);
}

STATIC
int
NullInit (
  VOID
  )
{
  mLogged = 1;
  return 0;
}

STATIC
VOID
NullPoll (
  VOID
  )
{
}

STATIC
pm_metal_net_sock_h
NullSocket (
  uint32_t  domain,
  uint32_t  type
  )
{
  UINT32  i;

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

STATIC
VOID
NullClose (
  pm_metal_net_sock_h  h
  )
{
  if (h == 0 || h > PM_METAL_NET_MAX_SOCKS) {
    return;
  }

  ZeroMem (&mSocks[h], sizeof (mSocks[h]));
}

STATIC
pm_metal_async_handle_t
NullConnect (
  pm_metal_net_sock_h  h,
  CONST CHAR8         *host,
  uint32_t             port
  )
{
  (VOID)host;
  (VOID)port;
  if (h == 0 || h > PM_METAL_NET_MAX_SOCKS || !mSocks[h].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return NullFailAsync ();
}

STATIC
pm_metal_async_handle_t
NullListen (
  pm_metal_net_sock_h  h,
  uint32_t             port
  )
{
  (VOID)port;
  if (h == 0 || !mSocks[h].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return NullFailAsync ();
}

STATIC
pm_metal_async_handle_t
NullAccept (
  pm_metal_net_sock_h  h
  )
{
  if (h == 0 || !mSocks[h].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return NullFailAsync ();
}

STATIC
uint32_t
NullSend (
  pm_metal_net_sock_h  h,
  CONST VOID          *ptr,
  uint32_t             len
  )
{
  (VOID)ptr;
  (VOID)len;
  (VOID)h;
  return 0;
}

STATIC
pm_metal_async_handle_t
NullRecv (
  pm_metal_net_sock_h  h,
  VOID                *ptr,
  uint32_t             len
  )
{
  (VOID)ptr;
  (VOID)len;
  if (h == 0 || !mSocks[h].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return NullFailAsync ();
}

STATIC
pm_metal_async_handle_t
NullDns (
  CONST CHAR8  *host
  )
{
  (VOID)host;
  return NullFailAsync ();
}

STATIC CONST pm_metal_net_ops_t  mNullOps = {
  "null",
  NullInit,
  NullPoll,
  NullSocket,
  NullClose,
  NullConnect,
  NullListen,
  NullAccept,
  NullSend,
  NullRecv,
  NullDns
};

void
pm_metal_net_null_install (
  VOID
  )
{
  pm_metal_net_set_ops (&mNullOps);
  (VOID)NullInit ();
}
