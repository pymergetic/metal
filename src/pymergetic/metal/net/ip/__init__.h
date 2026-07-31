#ifndef PYMERGETIC_METAL_NET_IP_H_
#define PYMERGETIC_METAL_NET_IP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pm_metal_net_ip_l2_rx_fn)(void *ctx, const uint8_t *frame, uint32_t len);
typedef void (*pm_metal_net_ip_l2_poll_fn)(pm_metal_net_ip_l2_rx_fn fn, void *ctx);

/** Device-agnostic L2 callbacks (virtio-net, bge, ...). Frames only. */
typedef struct pm_metal_net_ip_l2_ops {
  int (*open)(uint8_t mac_out[6]);
  const uint8_t *(*mac)(void);
  int (*tx)(const void *frame, uint32_t len);
  pm_metal_net_ip_l2_poll_fn poll;
} pm_metal_net_ip_l2_ops_t;

uint32_t pm_metal_net_ip_dns(const char *host);
int32_t  pm_metal_net_ip_dns_last_ntoa(char *out, uint32_t out_cap);

void pm_metal_net_ip_poll(void);

uint32_t pm_metal_net_ip_if_count(void);
int32_t  pm_metal_net_ip_if_status_index(uint32_t index, char *dest, uint32_t dest_cap);

/**
 * Attach an L2 driver under lwIP (adds ethN, starts DHCP).
 * `backend` is a status label only (e.g. "lwip+virtio-net", "lwip+bge").
 * Multiple NICs may be started — each becomes eth0, eth1, ...
 */
int pm_metal_net_ip_l2_start(const char *backend, const pm_metal_net_ip_l2_ops_t *ops);

/** Bring up loopback (127.0.0.1/8). Returns 0 on success. */
int pm_metal_net_ip_loopback_start(void);

/**
 * Returns 1 if named iface has a DHCP lease, 0 if pending, -1 on error.
 * When ip_out is non-NULL and ready, fills dotted IPv4.
 */
int pm_metal_net_ip_if_dhcp_ready(const char *ifname, char *ip_out, uint32_t ip_cap);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_IP_H_ */
