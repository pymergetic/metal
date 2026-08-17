/* pymergetic.metal.net.dhcp — DHCPv4 on ip UDP: the DORA client's lease, and
 * the in-process DISCOVER/OFFER server the loopback prove talks to. */
#ifndef PYMERGETIC_METAL_NET_DHCP_TYPES_H
#define PYMERGETIC_METAL_NET_DHCP_TYPES_H

#include "pymergetic/util/mem/__types__.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What the server granted. Every field is network order except lease_sec, and
 * every one but addr_be may be absent (0) — a server need not offer a router,
 * a resolver or a mask, and we must not invent one. */
typedef struct pm_metal_net_dhcp_lease {
    uint32_t addr_be;
    uint32_t mask_be;
    uint32_t gw_be;
    uint32_t dns_be;
    uint32_t server_be;
    uint32_t lease_sec;
} pm_metal_net_dhcp_lease_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_DHCP_TYPES_H */
