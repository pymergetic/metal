/** @file
  Metal net facade — pluggable ops (virtio-net / null). (impl: efi|bios)
**/
#include <pymergetic/metal/dev/net/net.h>
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/dev/net/net_cfg.h>

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>

#include "wasm_export.h"

STATIC CONST pm_metal_net_ops_t  *mOps;
STATIC wasm_module_inst_t         mNetInst;

void
pm_metal_net_set_ops (
  CONST pm_metal_net_ops_t  *ops
  )
{
  mOps = ops;
}

CONST pm_metal_net_ops_t *
pm_metal_net_get_ops (
  VOID
  )
{
  return mOps;
}

void
pm_metal_net_poll (
  VOID
  )
{
  if (mOps != NULL && mOps->poll != NULL) {
    mOps->poll ();
  }
}

void
pm_metal_net_bind_inst (
  VOID  *module_inst
  )
{
  mNetInst = (wasm_module_inst_t)module_inst;
}

pm_metal_net_sock_h
pm_metal_net_socket (
  uint32_t  domain,
  uint32_t  type
  )
{
  if (mOps == NULL || mOps->socket == NULL) {
    return PM_METAL_NET_SOCK_INVALID;
  }

  return mOps->socket (domain, type);
}

void
pm_metal_net_close (
  pm_metal_net_sock_h  h
  )
{
  if (mOps != NULL && mOps->close != NULL) {
    mOps->close (h);
  }
}

int32_t
pm_metal_net_bind_if (
  pm_metal_net_sock_h  h,
  CONST CHAR8         *ifname
  )
{
  if (mOps == NULL || mOps->bind_if == NULL) {
    return -1;
  }

  return mOps->bind_if (h, ifname);
}

pm_metal_async_handle_t
pm_metal_net_connect (
  pm_metal_net_sock_h  h,
  CONST CHAR8         *host,
  uint32_t             port
  )
{
  if (mOps == NULL || mOps->connect == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return mOps->connect (h, host, port);
}

pm_metal_async_handle_t
pm_metal_net_listen (
  pm_metal_net_sock_h  h,
  uint32_t             port
  )
{
  if (mOps == NULL || mOps->listen == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return mOps->listen (h, port);
}

pm_metal_async_handle_t
pm_metal_net_accept (
  pm_metal_net_sock_h  h
  )
{
  if (mOps == NULL || mOps->accept == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return mOps->accept (h);
}

uint32_t
pm_metal_net_send (
  pm_metal_net_sock_h  h,
  CONST VOID          *ptr,
  uint32_t             len
  )
{
  if (mOps == NULL || mOps->send == NULL) {
    return 0;
  }

  return mOps->send (h, ptr, len);
}

pm_metal_async_handle_t
pm_metal_net_recv (
  pm_metal_net_sock_h  h,
  VOID                *ptr,
  uint32_t             len
  )
{
  if (mOps == NULL || mOps->recv == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return mOps->recv (h, ptr, len);
}

pm_metal_async_handle_t
pm_metal_net_dns (
  CONST CHAR8  *host
  )
{
  if (mOps == NULL || mOps->dns == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return mOps->dns (host);
}

STATIC
INT32
MetalNetGuestHost (
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
pm_metal_net_socket_native (
  wasm_exec_env_t  exec_env,
  UINT32           domain,
  UINT32           type
  )
{
  (VOID)exec_env;
  return pm_metal_net_socket (domain, type);
}

STATIC UINT32
pm_metal_net_connect_native (
  wasm_exec_env_t  exec_env,
  UINT32           h,
  CONST CHAR8     *host,
  UINT32           port
  )
{
  CHAR8  cleaned[256];

  if (MetalNetGuestHost (exec_env, host, cleaned, sizeof (cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_net_connect (h, cleaned, port);
}

STATIC UINT32
pm_metal_net_listen_native (
  wasm_exec_env_t  exec_env,
  UINT32           h,
  UINT32           port
  )
{
  (VOID)exec_env;
  return pm_metal_net_listen (h, port);
}

STATIC UINT32
pm_metal_net_accept_native (
  wasm_exec_env_t  exec_env,
  UINT32           h
  )
{
  (VOID)exec_env;
  return pm_metal_net_accept (h);
}

STATIC UINT32
pm_metal_net_send_native (
  wasm_exec_env_t  exec_env,
  UINT32           h,
  UINT32           ptr,
  UINT32           len
  )
{
  VOID  *native;

  (VOID)exec_env;
  if (mNetInst == NULL || len == 0) {
    return 0;
  }

  if (!wasm_runtime_validate_app_addr (mNetInst, ptr, len)) {
    return 0;
  }

  native = wasm_runtime_addr_app_to_native (mNetInst, ptr);
  return pm_metal_net_send (h, native, len);
}

STATIC UINT32
pm_metal_net_recv_native (
  wasm_exec_env_t  exec_env,
  UINT32           h,
  UINT32           ptr,
  UINT32           len
  )
{
  VOID  *native;

  (VOID)exec_env;
  if (mNetInst == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (len > 0 && !wasm_runtime_validate_app_addr (mNetInst, ptr, len)) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  native = (len > 0) ? wasm_runtime_addr_app_to_native (mNetInst, ptr) : NULL;
  return pm_metal_net_recv (h, native, len);
}

STATIC UINT32
pm_metal_net_dns_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *host
  )
{
  CHAR8  cleaned[256];

  if (MetalNetGuestHost (exec_env, host, cleaned, sizeof (cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_net_dns (cleaned);
}

STATIC INT32
pm_metal_net_dns_last_ntoa_native (
  wasm_exec_env_t  exec_env,
  UINT32           dest,
  UINT32           dest_cap
  )
{
  VOID  *native;

  (VOID)exec_env;
  if (mNetInst == NULL || dest_cap == 0) {
    return -1;
  }

  if (!wasm_runtime_validate_app_addr (mNetInst, dest, dest_cap)) {
    return -1;
  }

  native = wasm_runtime_addr_app_to_native (mNetInst, dest);
  if (native == NULL) {
    return -1;
  }

  return pm_metal_net_dns_last_ntoa ((CHAR8 *)native, dest_cap);
}

STATIC VOID
pm_metal_net_close_native (
  wasm_exec_env_t  exec_env,
  UINT32           h
  )
{
  (VOID)exec_env;
  pm_metal_net_close (h);
}

STATIC INT32
pm_metal_net_bind_if_native (
  wasm_exec_env_t  exec_env,
  UINT32           h,
  CONST CHAR8     *ifname
  )
{
  CHAR8  cleaned[PM_METAL_NET_IFNAME_MAX];

  if (ifname == NULL) {
    return pm_metal_net_bind_if (h, NULL);
  }

  if (MetalNetGuestHost (exec_env, ifname, cleaned, sizeof (cleaned)) != 0) {
    return -1;
  }

  return pm_metal_net_bind_if (h, cleaned);
}

STATIC NativeSymbol g_pm_metal_net_native_symbols[] = {
  { "pm_metal_net_socket", (VOID *)pm_metal_net_socket_native, "(ii)i", NULL },
  { "pm_metal_net_connect", (VOID *)pm_metal_net_connect_native, "(i$i)i", NULL },
  { "pm_metal_net_listen", (VOID *)pm_metal_net_listen_native, "(ii)i", NULL },
  { "pm_metal_net_accept", (VOID *)pm_metal_net_accept_native, "(i)i", NULL },
  { "pm_metal_net_send", (VOID *)pm_metal_net_send_native, "(iii)i", NULL },
  { "pm_metal_net_recv", (VOID *)pm_metal_net_recv_native, "(iii)i", NULL },
  { "pm_metal_net_dns", (VOID *)pm_metal_net_dns_native, "($)i", NULL },
  { "pm_metal_net_dns_last_ntoa", (VOID *)pm_metal_net_dns_last_ntoa_native,
    "(ii)i", NULL },
  { "pm_metal_net_close", (VOID *)pm_metal_net_close_native, "(i)", NULL },
  { "pm_metal_net_bind_if", (VOID *)pm_metal_net_bind_if_native, "(i$)i", NULL },
};

int
pm_metal_net_native_register (
  VOID
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_NET_WASI_MODULE,
         g_pm_metal_net_native_symbols,
         sizeof (g_pm_metal_net_native_symbols)
           / sizeof (g_pm_metal_net_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}
