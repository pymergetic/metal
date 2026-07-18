/*
 * UDP sockets — Metal net API. Same for guests (wasi import) and host.
 * Returned fds are opaque host handles; use only with this module's calls
 * (not WASI sock_* / wasi-libc fd numbers).
 */
#ifndef PYMERGETIC_METAL_NET_UDP_H_
#define PYMERGETIC_METAL_NET_UDP_H_

#include "pymergetic/metal/net/addr.h"

#include <stdint.h>

#include "pymergetic/metal/wasi.h" /* IWYU pragma: keep */

#define PM_METAL_NET_UDP_WASI_MODULE "pymergetic.metal.net.udp"

#if defined(__wasm__)
#define PM_METAL_NET_UDP_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_NET_UDP_WASI_MODULE, name)
#endif

/*
 * impl: inline → port/udp (host)
 * impl: wasi import — src/common/pymergetic/metal/net/udp.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_net_udp_open(uint8_t family) PM_METAL_NET_UDP_IMPORT(pm_metal_net_udp_open);
extern int pm_metal_net_udp_close(int fd) PM_METAL_NET_UDP_IMPORT(pm_metal_net_udp_close);
extern int pm_metal_net_udp_set_timeout_ms(int fd, int32_t timeout_ms)
	PM_METAL_NET_UDP_IMPORT(pm_metal_net_udp_set_timeout_ms);
extern int pm_metal_net_udp_sendto(int fd, const void *buf, uint32_t len, const pm_metal_net_addr_t *to)
	PM_METAL_NET_UDP_IMPORT(pm_metal_net_udp_sendto);
extern int pm_metal_net_udp_recv(int fd, void *buf, uint32_t cap, uint32_t *out_len)
	PM_METAL_NET_UDP_IMPORT(pm_metal_net_udp_recv);
#else
#include "pymergetic/metal/port/udp.h"

static inline int pm_metal_net_udp_open(uint8_t family)
{
	return pm_metal_port_udp_open(family);
}

static inline int pm_metal_net_udp_close(int fd)
{
	return pm_metal_port_udp_close(fd);
}

static inline int pm_metal_net_udp_set_timeout_ms(int fd, int32_t timeout_ms)
{
	return pm_metal_port_udp_set_timeout_ms(fd, timeout_ms);
}

static inline int pm_metal_net_udp_sendto(int fd, const void *buf, uint32_t len,
					  const pm_metal_net_addr_t *to)
{
	return pm_metal_port_udp_sendto(fd, buf, len, to);
}

static inline int pm_metal_net_udp_recv(int fd, void *buf, uint32_t cap, uint32_t *out_len)
{
	return pm_metal_port_udp_recv(fd, buf, cap, out_len);
}
#endif

#if !defined(__wasm__)
/* impl: common — src/common/pymergetic/metal/net/udp.c */
int pm_metal_net_udp_native_register(void);
#endif

#endif /* PYMERGETIC_METAL_NET_UDP_H_ */
