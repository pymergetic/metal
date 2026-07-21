/*
 * Metal net — guest/host dual ABI (null backend first).
 * Not WASI sockets. See docs/IO.md.
 */
#ifndef PYMERGETIC_METAL_NET_H_
#define PYMERGETIC_METAL_NET_H_

#include <stdint.h>

#include "pymergetic/metal/async/async.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_NET_WASI_MODULE "pymergetic.metal.net"

typedef uint32_t pm_metal_net_sock_h;

#define PM_METAL_NET_SOCK_INVALID 0u

#define PM_METAL_NET_AF_INET  1u
#define PM_METAL_NET_SOCK_STREAM 1u
#define PM_METAL_NET_SOCK_DGRAM  2u

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

int pm_metal_net_native_register(void);
void pm_metal_net_bind_inst(void *module_inst);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_H_ */
