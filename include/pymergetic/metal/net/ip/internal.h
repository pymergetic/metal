#ifndef PM_METAL_NET_IP_INTERNAL_H_
#define PM_METAL_NET_IP_INTERNAL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pm_metal_net_ip_l4_rx_fn)(const uint8_t *ip_pkt, uint32_t ip_len, uint32_t ihl);

void pm_metal_net_ip_register_udp_rx(pm_metal_net_ip_l4_rx_fn fn);
void pm_metal_net_ip_register_tcp_rx(pm_metal_net_ip_l4_rx_fn fn);

const uint8_t *pm_metal_net_ip_mac(void);
uint32_t pm_metal_net_ip_addr_host(void);

/* 1 = resolved, 0 = request sent, -1 = fail/not ready */
int32_t pm_metal_net_ip_arp_resolve(uint32_t ip_host);
const uint8_t *pm_metal_net_ip_arp_lookup(uint32_t ip_host);
void pm_metal_net_ip_arp_cache_put(uint32_t ip_host, const uint8_t mac[6]);

/* TX L4 segment; resolves ARP for on-link or gateway. -2 = ARP pending. */
int32_t pm_metal_net_ip_tx_l4(uint32_t dst_ip_host, uint8_t proto,
                           const uint8_t *l4, uint32_t l4_len);

uint16_t pm_metal_net_ip_checksum(const uint8_t *data, uint32_t len);
uint16_t pm_metal_net_ip_l4_checksum(uint32_t src_ip, uint32_t dst_ip, uint8_t proto,
                                 const uint8_t *seg, uint32_t seg_len);

#ifdef __cplusplus
}
#endif

#endif
