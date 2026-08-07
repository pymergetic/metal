#ifndef PM_METAL_NET_DNS_H_
#define PM_METAL_NET_DNS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Resolve name to IPv4 (host uint32 in network byte layout, e.g. 0x0a000203).
 * Dotted literals short-circuit. Uses pm_metal_ip_dns() as the server.
 * Returns 0 on success, -1 on error/NXDOMAIN, -2 on timeout.
 */
int32_t pm_metal_dns_resolve(const char *name, uint32_t *addr_out);

#ifdef __cplusplus
}
#endif

#endif
