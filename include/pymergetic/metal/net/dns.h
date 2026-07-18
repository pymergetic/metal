/*
 * DNS resolve — Metal net API. Same for guests (wasi import) and host.
 * Complements WASI sockets; fds/addrs here are host-side via this module.
 */
#ifndef PYMERGETIC_METAL_NET_DNS_H_
#define PYMERGETIC_METAL_NET_DNS_H_

#include "pymergetic/metal/net/addr.h"

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/wasi.h" /* IWYU pragma: keep */

#define PM_METAL_NET_DNS_WASI_MODULE "pymergetic.metal.net.dns"

#if defined(__wasm__)
#define PM_METAL_NET_DNS_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_NET_DNS_WASI_MODULE, name)
#endif

/*
 * Resolve host to addresses for service port (host order).
 * Fills out[0..*out_n). Returns 0 with *out_n >= 1, or -1.
 *
 * impl: inline → port/dns (host)
 * impl: wasi import — src/common/pymergetic/metal/net/dns.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_net_dns_lookup(const char *host, uint16_t port, pm_metal_net_addr_t *out,
				   size_t out_cap, size_t *out_n)
	PM_METAL_NET_DNS_IMPORT(pm_metal_net_dns_lookup);
#else
#include "pymergetic/metal/port/dns.h"

static inline int pm_metal_net_dns_lookup(const char *host, uint16_t port, pm_metal_net_addr_t *out,
					  size_t out_cap, size_t *out_n)
{
	return pm_metal_port_dns_lookup(host, port, out, out_cap, out_n);
}
#endif

#if !defined(__wasm__)
/* impl: common — src/common/pymergetic/metal/net/dns.c */
int pm_metal_net_dns_native_register(void);
#endif

#endif /* PYMERGETIC_METAL_NET_DNS_H_ */
