/*
 * Metal stateful DHCPv6 client (lwIP stateless DHCPv6 stays in dhcp6.c).
 * impl: common — src/pymergetic/metal/dev/net/metal_dhcp6_stateful.c
 */
#ifndef PYMERGETIC_METAL_DEV_NET_METAL_DHCP6_STATEFUL_H_
#define PYMERGETIC_METAL_DEV_NET_METAL_DHCP6_STATEFUL_H_

#include "lwip/err.h"
#include "lwip/netif.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct metal_dhcp6_stateful {
	u8_t  state;
	u8_t  tries;
	u16_t timeout_ticks;
	u32_t xid;
	u32_t iaid;
	u16_t server_id_len;
	u8_t  server_id[64];
	u8_t  bound;
} metal_dhcp6_stateful_t;

void metal_dhcp6_stateful_reset(metal_dhcp6_stateful_t *st);
err_t metal_dhcp6_stateful_start(struct netif *netif, metal_dhcp6_stateful_t *st);
void metal_dhcp6_stateful_stop(struct netif *netif, metal_dhcp6_stateful_t *st);
void metal_dhcp6_stateful_poll(struct netif *netif, metal_dhcp6_stateful_t *st);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_NET_METAL_DHCP6_STATEFUL_H_ */
