/*
 * Metal net — guest/host dual ABI (null backend first).
 * Not WASI sockets. See docs/IO.md.
 *
 * Async: connect, listen, accept, recv, dns → await → pm_metal_net_result().
 * Sync:  socket, send, close.
 *
 * impl: common — src/pymergetic/metal/dev/net/net.c
 */
#ifndef PYMERGETIC_METAL_DEV_NET_NET_H_
#define PYMERGETIC_METAL_DEV_NET_NET_H_

#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_NET_WASI_MODULE "pymergetic.metal.net"

typedef uint32_t pm_metal_net_sock_h;

#define PM_METAL_NET_SOCK_INVALID 0u

#define PM_METAL_NET_AF_INET   1u
#define PM_METAL_NET_AF_INET6  2u
#define PM_METAL_NET_SOCK_STREAM 1u
#define PM_METAL_NET_SOCK_DGRAM  2u

/** Guest linear offset (wasm) or host pointer — for send/recv buffer args. */
#if defined(__wasm__)
#define PM_METAL_NET_IO_PTR(p) ((uint32_t)(uintptr_t)(p))
#else
#define PM_METAL_NET_IO_PTR(p) (p)
#endif

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_NET_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_NET_WASI_MODULE, name)

extern pm_metal_net_sock_h pm_metal_net_socket(uint32_t domain, uint32_t type)
	PM_METAL_NET_IMPORT(pm_metal_net_socket);
extern pm_metal_async_handle_t pm_metal_net_connect(pm_metal_net_sock_h h,
						    const char *host,
						    uint32_t port)
	PM_METAL_NET_IMPORT(pm_metal_net_connect);
extern pm_metal_async_handle_t pm_metal_net_listen(pm_metal_net_sock_h h,
						   uint32_t port)
	PM_METAL_NET_IMPORT(pm_metal_net_listen);
extern pm_metal_async_handle_t pm_metal_net_accept(pm_metal_net_sock_h h)
	PM_METAL_NET_IMPORT(pm_metal_net_accept);
extern uint32_t pm_metal_net_send(pm_metal_net_sock_h h, uint32_t ptr,
				  uint32_t len)
	PM_METAL_NET_IMPORT(pm_metal_net_send);
extern pm_metal_async_handle_t pm_metal_net_recv(pm_metal_net_sock_h h,
						 uint32_t ptr, uint32_t len)
	PM_METAL_NET_IMPORT(pm_metal_net_recv);
extern pm_metal_async_handle_t pm_metal_net_dns(const char *host)
	PM_METAL_NET_IMPORT(pm_metal_net_dns);
extern void pm_metal_net_close(pm_metal_net_sock_h h)
	PM_METAL_NET_IMPORT(pm_metal_net_close);
/** Bind socket to interface ("eth0", "eth1"). NULL → default. Before connect/listen. */
extern int32_t pm_metal_net_bind_if(pm_metal_net_sock_h h, const char *ifname)
	PM_METAL_NET_IMPORT(pm_metal_net_bind_if);

/** After await on connect/recv/accept/dns: bytes, new sock handle, or 1/0. */
static inline uint32_t pm_metal_net_result(pm_metal_async_handle_t self_h)
{
	return pm_metal_async_result_u32(self_h);
}
#else
pm_metal_net_sock_h pm_metal_net_socket(uint32_t domain, uint32_t type);
pm_metal_async_handle_t pm_metal_net_connect(pm_metal_net_sock_h h,
					     const char *host, uint32_t port);
pm_metal_async_handle_t pm_metal_net_listen(pm_metal_net_sock_h h,
					    uint32_t port);
pm_metal_async_handle_t pm_metal_net_accept(pm_metal_net_sock_h h);
uint32_t pm_metal_net_send(pm_metal_net_sock_h h, const void *ptr, uint32_t len);
pm_metal_async_handle_t pm_metal_net_recv(pm_metal_net_sock_h h, void *ptr,
					  uint32_t len);
pm_metal_async_handle_t pm_metal_net_dns(const char *host);
void pm_metal_net_close(pm_metal_net_sock_h h);
/** Bind socket to named interface (eth0..). ifname NULL → default. Returns 0 or -1. */
int pm_metal_net_bind_if(pm_metal_net_sock_h h, const char *ifname);

static inline uint32_t pm_metal_net_result(pm_metal_async_handle_t self_h)
{
	return pm_metal_async_result_u32(self_h);
}

int pm_metal_net_native_register(void);
void pm_metal_net_bind_inst(void *module_inst);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_NET_NET_H_ */
