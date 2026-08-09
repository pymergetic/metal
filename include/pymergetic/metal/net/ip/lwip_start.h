#ifndef PYMERGETIC_METAL_NET_IP_LWIP_START_H_
#define PYMERGETIC_METAL_NET_IP_LWIP_START_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pm_metal_net_ip_l2_rx_fn)(void *ctx, const uint8_t *frame, uint32_t len);
typedef void (*pm_metal_net_ip_l2_poll_fn)(pm_metal_net_ip_l2_rx_fn fn, void *ctx);

int pm_metal_net_ip_lwip_start_with_l2(const char *backend, int (*open_fn)(uint8_t mac_out[6]),
                                       const uint8_t *(*mac_fn)(void),
                                       int (*tx_fn)(const void *frame, uint32_t len),
                                       pm_metal_net_ip_l2_poll_fn poll_fn);
int pm_metal_net_ip_loopback_start(void);
int pm_metal_net_ip_virtio_start(void);
int pm_metal_net_ip_bge_start(void);

/* Wait until eth0 has a DHCPv4 lease (or timeout). 1=leased, 0=pending, -1=err. */
int pm_metal_net_ip_if_dhcp_ready(const char *name, char *ip_out, uint32_t ip_cap);

/**
 * Reserve an iface slot and return its embedded struct netif* for netif_add.
 * Pass netif=NULL. Returns NULL on failure.
 */
void *pm_metal_net_ip_register_netif(const char *name, const char *backend, void *unused);
void pm_metal_net_ip_unregister_named(const char *name);
void pm_metal_net_ip_bump_if_gen(void);
/* Host-order dotted IPv4 → lwIP ip4_addr_t. */
int pm_metal_ip_parse_ipv4(const char *s, void *ip4_addr_out);

#ifdef __cplusplus
}
#endif

#endif
