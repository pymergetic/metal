/** @file
  Metal net facade — pluggable ops (virtio-net / null). (impl: efi|bios)
**/
#include <pymergetic/metal/net/ip/ip.h>
#include <pymergetic/metal/net/ip/ip_ops.h>
#include <pymergetic/metal/net/ip/ip_cfg.h>

#include <stddef.h>
#include <stdint.h>

#include "wasm_export.h"

static const pm_metal_net_ip_ops_t *mOps;

void pm_metal_net_ip_set_ops(const pm_metal_net_ip_ops_t *ops)
{
  mOps = ops;
}

const pm_metal_net_ip_ops_t *pm_metal_net_ip_get_ops(void)
{
  return mOps;
}

void pm_metal_net_ip_poll(void)
{
  if (mOps != NULL && mOps->poll != NULL) {
    mOps->poll();
  }
}

pm_metal_net_ip_sock_h pm_metal_net_ip_socket(uint32_t domain, uint32_t type)
{
  if (mOps == NULL || mOps->socket == NULL) {
    return PM_METAL_NET_IP_SOCK_INVALID;
  }

  return mOps->socket(domain, type);
}

void pm_metal_net_ip_close(pm_metal_net_ip_sock_h h)
{
  if (mOps != NULL && mOps->close != NULL) {
    mOps->close(h);
  }
}

int32_t pm_metal_net_ip_bind_if(pm_metal_net_ip_sock_h h, const char *ifname)
{
  if (mOps == NULL || mOps->bind_if == NULL) {
    return -1;
  }

  return mOps->bind_if(h, ifname);
}

pm_metal_async_handle_t pm_metal_net_ip_connect(pm_metal_net_ip_sock_h h,
                                                const char            *host,
                                                uint32_t               port)
{
  if (mOps == NULL || mOps->connect == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return mOps->connect(h, host, port);
}

pm_metal_async_handle_t pm_metal_net_ip_listen(pm_metal_net_ip_sock_h h, uint32_t port)
{
  if (mOps == NULL || mOps->listen == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return mOps->listen(h, port);
}

pm_metal_async_handle_t pm_metal_net_ip_accept(pm_metal_net_ip_sock_h h)
{
  if (mOps == NULL || mOps->accept == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return mOps->accept(h);
}

uint32_t pm_metal_net_ip_send(pm_metal_net_ip_sock_h h, const void *ptr, uint32_t len)
{
  if (mOps == NULL || mOps->send == NULL) {
    return 0;
  }

  return mOps->send(h, ptr, len);
}

pm_metal_async_handle_t pm_metal_net_ip_recv(pm_metal_net_ip_sock_h h, void *ptr, uint32_t len)
{
  if (mOps == NULL || mOps->recv == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return mOps->recv(h, ptr, len);
}

uint32_t pm_metal_net_ip_try_recv(pm_metal_net_ip_sock_h h, void *ptr, uint32_t len)
{
  if (mOps == NULL || mOps->try_recv == NULL) {
    return 0;
  }

  return mOps->try_recv(h, ptr, len);
}

int32_t pm_metal_net_ip_bind(pm_metal_net_ip_sock_h h, uint32_t port)
{
  if (mOps == NULL || mOps->bind == NULL) {
    return -1;
  }

  return mOps->bind(h, port);
}

uint32_t pm_metal_net_ip_sendto(
  pm_metal_net_ip_sock_h h, const void *ptr, uint32_t len, const char *host, uint32_t port)
{
  if (mOps == NULL || mOps->sendto == NULL) {
    return 0;
  }

  return mOps->sendto(h, ptr, len, host, port);
}

uint32_t pm_metal_net_ip_try_recvfrom(pm_metal_net_ip_sock_h h,
                                      void                  *ptr,
                                      uint32_t               len,
                                      char                  *peer_host,
                                      uint32_t               peer_cap,
                                      uint32_t              *peer_port)
{
  if (mOps == NULL || mOps->try_recvfrom == NULL) {
    return 0;
  }

  return mOps->try_recvfrom(h, ptr, len, peer_host, peer_cap, peer_port);
}

pm_metal_async_handle_t pm_metal_net_ip_dns(const char *host)
{
  if (mOps == NULL || mOps->dns == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return mOps->dns(host);
}

static int32_t MetalNetGuestHost(wasm_exec_env_t exec_env,
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

static uint32_t pm_metal_net_ip_socket_native(wasm_exec_env_t exec_env,
                                              uint32_t        domain,
                                              uint32_t        type)
{
  (void)exec_env;
  return pm_metal_net_ip_socket(domain, type);
}

static uint32_t pm_metal_net_ip_connect_native(wasm_exec_env_t exec_env,
                                               uint32_t        h,
                                               const char     *host,
                                               uint32_t        port)
{
  char cleaned[256];

  if (MetalNetGuestHost(exec_env, host, cleaned, sizeof(cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_net_ip_connect(h, cleaned, port);
}

static uint32_t pm_metal_net_ip_listen_native(wasm_exec_env_t exec_env, uint32_t h, uint32_t port)
{
  (void)exec_env;
  return pm_metal_net_ip_listen(h, port);
}

static uint32_t pm_metal_net_ip_accept_native(wasm_exec_env_t exec_env, uint32_t h)
{
  (void)exec_env;
  return pm_metal_net_ip_accept(h);
}

static uint32_t pm_metal_net_ip_send_native(wasm_exec_env_t exec_env,
                                            uint32_t        h,
                                            uint32_t        ptr,
                                            uint32_t        len)
{
  void *native;

  if (len == 0) {
    return 0;
  }

  native = pm_metal_async_guest_buf_durable(exec_env, ptr, len);
  if (native == NULL) {
    return 0;
  }

  return pm_metal_net_ip_send(h, native, len);
}

static uint32_t pm_metal_net_ip_recv_native(wasm_exec_env_t exec_env,
                                            uint32_t        h,
                                            uint32_t        ptr,
                                            uint32_t        len)
{
  void *native;

  native = (len > 0) ? pm_metal_async_guest_buf_durable(exec_env, ptr, len) : NULL;
  if (len > 0 && native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_net_ip_recv(h, native, len);
}

static uint32_t pm_metal_net_ip_dns_native(wasm_exec_env_t exec_env, const char *host)
{
  char cleaned[256];

  if (MetalNetGuestHost(exec_env, host, cleaned, sizeof(cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_net_ip_dns(cleaned);
}

static int32_t pm_metal_net_ip_dns_last_ntoa_native(wasm_exec_env_t exec_env,
                                                    uint32_t        dest,
                                                    uint32_t        dest_cap)
{
  wasm_module_inst_t inst;
  void              *native;

  if (dest_cap == 0) {
    return -1;
  }

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL) {
    return -1;
  }

  if (!wasm_runtime_validate_app_addr(inst, dest, dest_cap)) {
    return -1;
  }

  native = wasm_runtime_addr_app_to_native(inst, dest);
  if (native == NULL) {
    return -1;
  }

  return pm_metal_net_ip_dns_last_ntoa((char *)native, dest_cap);
}

static int32_t pm_metal_net_ip_seed_host_native(wasm_exec_env_t exec_env,
                                                uint32_t        dest,
                                                uint32_t        dest_cap)
{
  wasm_module_inst_t inst;
  void              *native;

  if (dest_cap == 0) {
    return -1;
  }

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL) {
    return -1;
  }

  if (!wasm_runtime_validate_app_addr(inst, dest, dest_cap)) {
    return -1;
  }

  native = wasm_runtime_addr_app_to_native(inst, dest);
  if (native == NULL) {
    return -1;
  }

  return pm_metal_net_ip_seed_host((char *)native, dest_cap);
}

static void pm_metal_net_ip_close_native(wasm_exec_env_t exec_env, uint32_t h)
{
  (void)exec_env;
  pm_metal_net_ip_close(h);
}

static int32_t pm_metal_net_ip_bind_if_native(wasm_exec_env_t exec_env,
                                              uint32_t        h,
                                              const char     *ifname)
{
  char cleaned[PM_METAL_NET_IP_IFNAME_MAX];

  if (ifname == NULL) {
    return pm_metal_net_ip_bind_if(h, NULL);
  }

  if (MetalNetGuestHost(exec_env, ifname, cleaned, sizeof(cleaned)) != 0) {
    return -1;
  }

  return pm_metal_net_ip_bind_if(h, cleaned);
}

static int32_t pm_metal_net_ip_bind_native(wasm_exec_env_t exec_env, uint32_t h, uint32_t port)
{
  (void)exec_env;
  return pm_metal_net_ip_bind(h, port);
}

static uint32_t pm_metal_net_ip_sendto_native(
  wasm_exec_env_t exec_env, uint32_t h, uint32_t ptr, uint32_t len, const char *host, uint32_t port)
{
  void *native;
  char  cleaned[256];

  if (len == 0) {
    return 0;
  }
  native = pm_metal_async_guest_buf_durable(exec_env, ptr, len);
  if (native == NULL) {
    return 0;
  }
  if (MetalNetGuestHost(exec_env, host, cleaned, sizeof(cleaned)) != 0) {
    return 0;
  }
  return pm_metal_net_ip_sendto(h, native, len, cleaned, port);
}

static uint32_t pm_metal_net_ip_try_recv_native(wasm_exec_env_t exec_env,
                                                uint32_t        h,
                                                uint32_t        ptr,
                                                uint32_t        len)
{
  void *native;

  if (len == 0) {
    return 0;
  }
  native = pm_metal_async_guest_buf_durable(exec_env, ptr, len);
  if (native == NULL) {
    return (uint32_t)-1;
  }
  return pm_metal_net_ip_try_recv(h, native, len);
}

static uint32_t pm_metal_net_ip_try_recvfrom_native(wasm_exec_env_t exec_env,
                                                    uint32_t        h,
                                                    uint32_t        ptr,
                                                    uint32_t        len,
                                                    uint32_t        peer_host,
                                                    uint32_t        peer_cap,
                                                    uint32_t        peer_port_ptr)
{
  wasm_module_inst_t inst;
  void              *native;
  char              *peer;
  uint32_t          *pport;
  uint32_t           n;
  uint32_t           port;

  if (len == 0) {
    return 0;
  }
  native = pm_metal_async_guest_buf_durable(exec_env, ptr, len);
  if (native == NULL) {
    return (uint32_t)-1;
  }
  inst  = wasm_runtime_get_module_inst(exec_env);
  peer  = NULL;
  pport = NULL;
  if (peer_host != 0 && peer_cap > 0 && inst != NULL &&
      wasm_runtime_validate_app_addr(inst, peer_host, peer_cap)) {
    peer = (char *)wasm_runtime_addr_app_to_native(inst, peer_host);
  }
  if (peer_port_ptr != 0 && inst != NULL &&
      wasm_runtime_validate_app_addr(inst, peer_port_ptr, sizeof(uint32_t))) {
    pport = (uint32_t *)wasm_runtime_addr_app_to_native(inst, peer_port_ptr);
  }
  port = 0;
  n    = pm_metal_net_ip_try_recvfrom(h, native, len, peer, peer_cap, &port);
  if (pport != NULL && n > 0 && n != (uint32_t)-1) {
    *pport = port;
  }
  return n;
}

static uint32_t pm_metal_net_ip_if_count_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return (uint32_t)pm_metal_net_ip_if_count();
}

static uint32_t pm_metal_net_ip_if_gen_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_net_ip_if_gen();
}

static uint32_t pm_metal_net_ip_if_wait_native(wasm_exec_env_t exec_env, uint32_t since_gen)
{
  (void)exec_env;
  return pm_metal_net_ip_if_wait(since_gen);
}

static int32_t pm_metal_net_ip_if_status_index_native(wasm_exec_env_t exec_env,
                                                      uint32_t        index,
                                                      uint32_t        dest,
                                                      uint32_t        dest_cap)
{
  wasm_module_inst_t inst;
  void              *native;

  if (dest_cap == 0) {
    return -1;
  }
  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || !wasm_runtime_validate_app_addr(inst, dest, dest_cap)) {
    return -1;
  }
  native = wasm_runtime_addr_app_to_native(inst, dest);
  if (native == NULL) {
    return -1;
  }
  return pm_metal_net_ip_if_status_index(index, (char *)native, dest_cap);
}

static NativeSymbol g_pm_metal_net_native_symbols[] = {
  { "pm_metal_net_ip_socket", (void *)pm_metal_net_ip_socket_native, "(ii)i", NULL },
  { "pm_metal_net_ip_connect", (void *)pm_metal_net_ip_connect_native, "(i$i)i", NULL },
  { "pm_metal_net_ip_listen", (void *)pm_metal_net_ip_listen_native, "(ii)i", NULL },
  { "pm_metal_net_ip_accept", (void *)pm_metal_net_ip_accept_native, "(i)i", NULL },
  { "pm_metal_net_ip_send", (void *)pm_metal_net_ip_send_native, "(iii)i", NULL },
  { "pm_metal_net_ip_recv", (void *)pm_metal_net_ip_recv_native, "(iii)i", NULL },
  { "pm_metal_net_ip_dns", (void *)pm_metal_net_ip_dns_native, "($)i", NULL },
  { "pm_metal_net_ip_dns_last_ntoa", (void *)pm_metal_net_ip_dns_last_ntoa_native, "(ii)i", NULL },
  { "pm_metal_net_ip_seed_host", (void *)pm_metal_net_ip_seed_host_native, "(ii)i", NULL },
  { "pm_metal_net_ip_close", (void *)pm_metal_net_ip_close_native, "(i)", NULL },
  { "pm_metal_net_ip_bind_if", (void *)pm_metal_net_ip_bind_if_native, "(i$)i", NULL },
  { "pm_metal_net_ip_bind", (void *)pm_metal_net_ip_bind_native, "(ii)i", NULL },
  { "pm_metal_net_ip_sendto", (void *)pm_metal_net_ip_sendto_native, "(iii$i)i", NULL },
  { "pm_metal_net_ip_try_recv", (void *)pm_metal_net_ip_try_recv_native, "(iii)i", NULL },
  { "pm_metal_net_ip_try_recvfrom",
    (void *)pm_metal_net_ip_try_recvfrom_native,
    "(iiiiii)i",
    NULL },
  { "pm_metal_net_ip_if_count", (void *)pm_metal_net_ip_if_count_native, "()i", NULL },
  { "pm_metal_net_ip_if_gen", (void *)pm_metal_net_ip_if_gen_native, "()i", NULL },
  { "pm_metal_net_ip_if_wait", (void *)pm_metal_net_ip_if_wait_native, "(i)i", NULL },
  { "pm_metal_net_ip_if_status_index",
    (void *)pm_metal_net_ip_if_status_index_native,
    "(iii)i",
    NULL },
};

int pm_metal_net_ip_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_NET_IP_WASI_MODULE,
                                     g_pm_metal_net_native_symbols,
                                     sizeof(g_pm_metal_net_native_symbols) /
                                       sizeof(g_pm_metal_net_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
