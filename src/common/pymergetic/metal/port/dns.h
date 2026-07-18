/*
 * Port — DNS resolve (OS floor). Bind only. Callers use net/dns.
 */
#ifndef PYMERGETIC_METAL_PORT_DNS_H_
#define PYMERGETIC_METAL_PORT_DNS_H_

#include "pymergetic/metal/net/addr.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Resolve host to addresses for service port (host order).
 * Fills out[0..*out_n). Returns 0 with *out_n >= 1, or -1.
 * impl: bind — src/<plat>/pymergetic/metal/port/dns.c
 */
int pm_metal_port_dns_lookup(const char *host, uint16_t port, pm_metal_net_addr_t *out, size_t out_cap,
			     size_t *out_n);

#endif /* PYMERGETIC_METAL_PORT_DNS_H_ */
