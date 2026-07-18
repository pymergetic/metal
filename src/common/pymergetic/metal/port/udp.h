/*
 * Port — UDP socket primitives (OS floor). Bind only. Callers use net/udp.
 */
#ifndef PYMERGETIC_METAL_PORT_UDP_H_
#define PYMERGETIC_METAL_PORT_UDP_H_

#include "pymergetic/metal/net/addr.h"

#include <stdint.h>

/* impl: bind — src/<plat>/pymergetic/metal/port/udp.c */
int pm_metal_port_udp_open(uint8_t family);

/* impl: bind — src/<plat>/pymergetic/metal/port/udp.c */
int pm_metal_port_udp_close(int fd);

/* impl: bind — timeout_ms <= 0 → default (~3000). */
int pm_metal_port_udp_set_timeout_ms(int fd, int32_t timeout_ms);

/* impl: bind — src/<plat>/pymergetic/metal/port/udp.c */
int pm_metal_port_udp_sendto(int fd, const void *buf, uint32_t len, const pm_metal_net_addr_t *to);

/* impl: bind — src/<plat>/pymergetic/metal/port/udp.c */
int pm_metal_port_udp_recv(int fd, void *buf, uint32_t cap, uint32_t *out_len);

#endif /* PYMERGETIC_METAL_PORT_UDP_H_ */
