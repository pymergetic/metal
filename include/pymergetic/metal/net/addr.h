/*
 * Portable IP address (no OS sockaddr). Shared by net/{dns,udp,tcp}.
 * Types only — no wasi imports.
 */
#ifndef PYMERGETIC_METAL_NET_ADDR_H_
#define PYMERGETIC_METAL_NET_ADDR_H_

#include <stdint.h>

#define PM_METAL_NET_AF_INET 4
#define PM_METAL_NET_AF_INET6 6

typedef struct pm_metal_net_addr {
	uint8_t family; /* PM_METAL_NET_AF_INET or _INET6 */
	uint16_t port; /* host byte order */
	uint8_t ip[16]; /* IPv4 in [0..3], IPv6 in [0..15] */
} pm_metal_net_addr_t;

#endif /* PYMERGETIC_METAL_NET_ADDR_H_ */
