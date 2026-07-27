/*
 * Metal net — guest/host dual ABI (null backend first).
 * Not WASI sockets. See docs/IO.md.
 *
 * Async: connect, listen, accept, recv, dns → await → pm_metal_net_ip_result().
 * After successful dns await: pm_metal_net_ip_dns_last_ntoa (address string).
 * Sync façade: socket, send, sendto, close, bind, bind_if, try_recv, try_recvfrom.
 *
 * impl: common — src/pymergetic/metal/net/ip/ip.c (+ ip_lwip.c)
 */
#ifndef PYMERGETIC_METAL_NET_IP_H_
#define PYMERGETIC_METAL_NET_IP_H_

#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_NET_IP_WASI_MODULE "pymergetic.metal.net.ip"

typedef uint32_t pm_metal_net_ip_sock_h;

#define PM_METAL_NET_IP_SOCK_INVALID 0u

#define PM_METAL_NET_IP_AF_INET     1u
#define PM_METAL_NET_IP_AF_INET6    2u
#define PM_METAL_NET_IP_SOCK_STREAM 1u
#define PM_METAL_NET_IP_SOCK_DGRAM  2u

/** Guest linear offset (wasm) or host pointer — for send/recv buffer args. */
#if defined(__wasm__)
#define PM_METAL_NET_IP_IO_PTR(p) ((uint32_t)(uintptr_t)(p))
#else
#define PM_METAL_NET_IP_IO_PTR(p) (p)
#endif

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_NET_IP_IMPORT(name) PM_METAL_WASI_IMPORT(PM_METAL_NET_IP_WASI_MODULE, name)

extern pm_metal_net_ip_sock_h pm_metal_net_ip_socket(uint32_t domain, uint32_t type)
  PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_socket);
extern pm_metal_async_handle_t pm_metal_net_ip_connect(pm_metal_net_ip_sock_h h,
                                                    const char         *host,
                                                    uint32_t            port)
  PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_connect);
extern pm_metal_async_handle_t pm_metal_net_ip_listen(pm_metal_net_ip_sock_h h, uint32_t port)
  PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_listen);
extern pm_metal_async_handle_t pm_metal_net_ip_accept(pm_metal_net_ip_sock_h h)
  PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_accept);
extern uint32_t pm_metal_net_ip_send(pm_metal_net_ip_sock_h h, uint32_t ptr, uint32_t len)
  PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_send);
extern pm_metal_async_handle_t pm_metal_net_ip_recv(pm_metal_net_ip_sock_h h, uint32_t ptr, uint32_t len)
  PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_recv);
extern pm_metal_async_handle_t pm_metal_net_ip_dns(const char *host)
  PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_dns);
/**
 * After await on pm_metal_net_ip_dns with result 1: last address as ASCII
 * (IPv4 or IPv6) into guest buffer. Returns 0, or -1 if none.
 */
extern int32_t pm_metal_net_ip_dns_last_ntoa(uint32_t dest, uint32_t dest_cap)
  PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_dns_last_ntoa);
/**
 * Best-effort LAN host for on-demand package/asset seeding (DHCP
 * next-server, else default gateway, else a lab fallback) as ASCII
 * "a.b.c.d" into guest buffer. Same resolution net_life.c uses for its own
 * HTTP seed fetches -- diagnostic only, does not imply a fetch was tried.
 * Returns 0, or -1 if it doesn't fit dest_cap.
 */
extern int32_t pm_metal_net_ip_seed_host(uint32_t dest, uint32_t dest_cap)
  PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_seed_host);
extern void pm_metal_net_ip_close(pm_metal_net_ip_sock_h h) PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_close);
/** Bind socket to interface ("eth0", "eth1"). NULL → default. Before connect/listen. */
extern int32_t pm_metal_net_ip_bind_if(pm_metal_net_ip_sock_h h, const char *ifname)
  PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_bind_if);
/** Bind local port (TCP or UDP). port 0 = ephemeral. Returns 0 or -1. */
extern int32_t pm_metal_net_ip_bind(pm_metal_net_ip_sock_h h, uint32_t port)
  PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_bind);
/** UDP sendto (ASCII host / dotted IP). Returns bytes sent, or 0. */
extern uint32_t pm_metal_net_ip_sendto(pm_metal_net_ip_sock_h h, uint32_t ptr, uint32_t len,
                                    const char *host, uint32_t port)
  PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_sendto);
/** Non-blocking recv: bytes, 0 empty, (uint32_t)-1 closed/error. */
extern uint32_t pm_metal_net_ip_try_recv(pm_metal_net_ip_sock_h h, uint32_t ptr, uint32_t len)
  PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_try_recv);
/**
 * Non-blocking UDP recvfrom: bytes, 0 empty, (uint32_t)-1 error.
 * peer_host/peer_cap receive ASCII peer; peer_port_ptr is a guest u32* or 0.
 */
extern uint32_t pm_metal_net_ip_try_recvfrom(pm_metal_net_ip_sock_h h, uint32_t ptr, uint32_t len,
                                          uint32_t peer_host, uint32_t peer_cap,
                                          uint32_t peer_port_ptr)
  PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_try_recvfrom);
extern uint32_t pm_metal_net_ip_if_count(void) PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_if_count);
extern uint32_t pm_metal_net_ip_if_gen(void) PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_if_gen);
extern pm_metal_async_handle_t pm_metal_net_ip_if_wait(uint32_t since_gen)
  PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_if_wait);
extern int32_t pm_metal_net_ip_if_status_index(uint32_t index, uint32_t dest, uint32_t dest_cap)
  PM_METAL_NET_IP_IMPORT(pm_metal_net_ip_if_status_index);

/** After await on connect/recv/accept/dns: bytes, new sock handle, or 1/0. */
static inline uint32_t pm_metal_net_ip_result(pm_metal_async_handle_t self_h)
{
  return pm_metal_async_result_u32(self_h);
}
#else
pm_metal_net_ip_sock_h     pm_metal_net_ip_socket(uint32_t domain, uint32_t type);
pm_metal_async_handle_t pm_metal_net_ip_connect(pm_metal_net_ip_sock_h h,
                                             const char         *host,
                                             uint32_t            port);
pm_metal_async_handle_t pm_metal_net_ip_listen(pm_metal_net_ip_sock_h h, uint32_t port);
pm_metal_async_handle_t pm_metal_net_ip_accept(pm_metal_net_ip_sock_h h);
uint32_t                pm_metal_net_ip_send(pm_metal_net_ip_sock_h h, const void *ptr, uint32_t len);
uint32_t                pm_metal_net_ip_sendto(pm_metal_net_ip_sock_h h, const void *ptr, uint32_t len,
                                            const char *host, uint32_t port);
pm_metal_async_handle_t pm_metal_net_ip_recv(pm_metal_net_ip_sock_h h, void *ptr, uint32_t len);
/** Non-blocking recv: bytes copied, 0 empty, (uint32_t)-1 closed/error. */
uint32_t                pm_metal_net_ip_try_recv(pm_metal_net_ip_sock_h h, void *ptr, uint32_t len);
/** Non-blocking UDP recvfrom with peer ASCII + port. */
uint32_t                pm_metal_net_ip_try_recvfrom(pm_metal_net_ip_sock_h h, void *ptr, uint32_t len,
                                                  char *peer_host, uint32_t peer_cap,
                                                  uint32_t *peer_port);
pm_metal_async_handle_t pm_metal_net_ip_dns(const char *host);
/**
 * After await on pm_metal_net_ip_dns with result 1: last address as ASCII
 * (IPv4 or IPv6). Returns 0, or -1 if none.
 */
int  pm_metal_net_ip_dns_last_ntoa(char *out, uint32_t out_cap);
/** Same resolution net_life.c uses for HTTP package seeding — see wasm decl above. */
int32_t pm_metal_net_ip_seed_host(char *out, uint32_t out_cap);
void pm_metal_net_ip_close(pm_metal_net_ip_sock_h h);
/** Bind socket to named interface (eth0..). ifname NULL → default. Returns 0 or -1. */
int32_t pm_metal_net_ip_bind_if(pm_metal_net_ip_sock_h h, const char *ifname);
/** Bind local port (TCP or UDP). port 0 = ephemeral. Returns 0 or -1. */
int32_t pm_metal_net_ip_bind(pm_metal_net_ip_sock_h h, uint32_t port);

/* if_count / if_get* live in ip_cfg.h (host). Gen/wait/status_index here too. */
uint32_t                pm_metal_net_ip_if_gen(void);
pm_metal_async_handle_t pm_metal_net_ip_if_wait(uint32_t since_gen);
int32_t                 pm_metal_net_ip_if_status_index(uint32_t index, char *dest, uint32_t dest_cap);

static inline uint32_t pm_metal_net_ip_result(pm_metal_async_handle_t self_h)
{
  return pm_metal_async_result_u32(self_h);
}

int pm_metal_net_ip_native_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_IP_H_ */
