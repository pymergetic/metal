/*
 * TCP sockets — Metal net API. Same for guests (wasi import) and host.
 * Returned fds are opaque host handles; use only with this module's calls
 * (not WASI sock_* / wasi-libc fd numbers).
 */
#ifndef PYMERGETIC_METAL_NET_TCP_H_
#define PYMERGETIC_METAL_NET_TCP_H_

#include "pymergetic/metal/net/addr.h"

#include <stdint.h>

#include "pymergetic/metal/wasi.h" /* IWYU pragma: keep */

#define PM_METAL_NET_TCP_WASI_MODULE "pymergetic.metal.net.tcp"

#if defined(__wasm__)
#define PM_METAL_NET_TCP_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_NET_TCP_WASI_MODULE, name)
#endif

/*
 * impl: inline → port/tcp (host)
 * impl: wasi import — src/common/pymergetic/metal/net/tcp.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_net_tcp_open(uint8_t family) PM_METAL_NET_TCP_IMPORT(pm_metal_net_tcp_open);
extern int pm_metal_net_tcp_close(int fd) PM_METAL_NET_TCP_IMPORT(pm_metal_net_tcp_close);
extern int pm_metal_net_tcp_set_timeout_ms(int fd, int32_t timeout_ms)
	PM_METAL_NET_TCP_IMPORT(pm_metal_net_tcp_set_timeout_ms);
extern int pm_metal_net_tcp_bind(int fd, const pm_metal_net_addr_t *addr)
	PM_METAL_NET_TCP_IMPORT(pm_metal_net_tcp_bind);
extern int pm_metal_net_tcp_listen(int fd, int backlog) PM_METAL_NET_TCP_IMPORT(pm_metal_net_tcp_listen);
extern int pm_metal_net_tcp_accept(int fd, pm_metal_net_addr_t *out_peer)
	PM_METAL_NET_TCP_IMPORT(pm_metal_net_tcp_accept);
extern int pm_metal_net_tcp_connect(int fd, const pm_metal_net_addr_t *to)
	PM_METAL_NET_TCP_IMPORT(pm_metal_net_tcp_connect);
extern int pm_metal_net_tcp_send(int fd, const void *buf, uint32_t len, uint32_t *out_sent)
	PM_METAL_NET_TCP_IMPORT(pm_metal_net_tcp_send);
extern int pm_metal_net_tcp_recv(int fd, void *buf, uint32_t cap, uint32_t *out_len)
	PM_METAL_NET_TCP_IMPORT(pm_metal_net_tcp_recv);
#else
#include "pymergetic/metal/port/tcp.h"

static inline int pm_metal_net_tcp_open(uint8_t family)
{
	return pm_metal_port_tcp_open(family);
}

static inline int pm_metal_net_tcp_close(int fd)
{
	return pm_metal_port_tcp_close(fd);
}

static inline int pm_metal_net_tcp_set_timeout_ms(int fd, int32_t timeout_ms)
{
	return pm_metal_port_tcp_set_timeout_ms(fd, timeout_ms);
}

static inline int pm_metal_net_tcp_bind(int fd, const pm_metal_net_addr_t *addr)
{
	return pm_metal_port_tcp_bind(fd, addr);
}

static inline int pm_metal_net_tcp_listen(int fd, int backlog)
{
	return pm_metal_port_tcp_listen(fd, backlog);
}

static inline int pm_metal_net_tcp_accept(int fd, pm_metal_net_addr_t *out_peer)
{
	return pm_metal_port_tcp_accept(fd, out_peer);
}

static inline int pm_metal_net_tcp_connect(int fd, const pm_metal_net_addr_t *to)
{
	return pm_metal_port_tcp_connect(fd, to);
}

static inline int pm_metal_net_tcp_send(int fd, const void *buf, uint32_t len, uint32_t *out_sent)
{
	return pm_metal_port_tcp_send(fd, buf, len, out_sent);
}

static inline int pm_metal_net_tcp_recv(int fd, void *buf, uint32_t cap, uint32_t *out_len)
{
	return pm_metal_port_tcp_recv(fd, buf, cap, out_len);
}
#endif

#if !defined(__wasm__)
/* impl: common — src/common/pymergetic/metal/net/tcp.c */
int pm_metal_net_tcp_native_register(void);
#endif

#endif /* PYMERGETIC_METAL_NET_TCP_H_ */
