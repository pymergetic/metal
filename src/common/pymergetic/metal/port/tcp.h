/*
 * Port — TCP socket primitives (OS floor). Bind only. Callers use net/tcp.
 */
#ifndef PYMERGETIC_METAL_PORT_TCP_H_
#define PYMERGETIC_METAL_PORT_TCP_H_

#include "pymergetic/metal/net/addr.h"

#include <stdint.h>

/* impl: bind — src/<plat>/pymergetic/metal/port/tcp.c */
int pm_metal_port_tcp_open(uint8_t family);

/* impl: bind — src/<plat>/pymergetic/metal/port/tcp.c */
int pm_metal_port_tcp_close(int fd);

/* impl: bind — timeout_ms <= 0 → default (~3000). */
int pm_metal_port_tcp_set_timeout_ms(int fd, int32_t timeout_ms);

/* impl: bind — src/<plat>/pymergetic/metal/port/tcp.c */
int pm_metal_port_tcp_bind(int fd, const pm_metal_net_addr_t *addr);

/* impl: bind — src/<plat>/pymergetic/metal/port/tcp.c */
int pm_metal_port_tcp_listen(int fd, int backlog);

/* Accept: returns new fd (>= 0) or -1. out_peer optional. */
int pm_metal_port_tcp_accept(int fd, pm_metal_net_addr_t *out_peer);

/* impl: bind — src/<plat>/pymergetic/metal/port/tcp.c */
int pm_metal_port_tcp_connect(int fd, const pm_metal_net_addr_t *to);

/* impl: bind — *out_sent optional; returns 0 / -1. */
int pm_metal_port_tcp_send(int fd, const void *buf, uint32_t len, uint32_t *out_sent);

/* impl: bind — *out_len set on success; returns 0 / -1. */
int pm_metal_port_tcp_recv(int fd, void *buf, uint32_t cap, uint32_t *out_len);

#endif /* PYMERGETIC_METAL_PORT_TCP_H_ */
