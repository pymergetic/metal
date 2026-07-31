/*
 * GENERATED
 * DO NOT HAND-EDIT THIS FILE.
 * This file is:  __init__.h
 * Edit instead:  __init__.rs
 * Source-sha: 55c3bcbff9eb0a08
 * Regenerate:    metal mod sync
 * Owned by:      metal mod sync (banner = write gate)
 */

#ifndef PM_METAL_PYMERGETIC_METAL_NET_IP_H_
#define PM_METAL_PYMERGETIC_METAL_NET_IP_H_

#include <stddef.h> /* IWYU pragma: keep */
#include <stdint.h> /* IWYU pragma: keep */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_metal_net_ip_l2_ops_t pm_metal_net_ip_l2_ops_t;

typedef void (*pm_metal_net_ip_l2_rx_fn)(void * ctx, const uint8_t * frame, uint32_t len);

typedef void (*pm_metal_net_ip_l2_poll_fn)(pm_metal_net_ip_l2_rx_fn rx, void * ctx);

typedef int32_t (*pm_metal_net_ip_l2_open_fn)(uint8_t * mac_out);

typedef const uint8_t * (*pm_metal_net_ip_l2_mac_fn)(void);

typedef int32_t (*pm_metal_net_ip_l2_tx_fn)(const void * frame, uint32_t len);

struct pm_metal_net_ip_l2_ops_t {
  pm_metal_net_ip_l2_open_fn open;
  pm_metal_net_ip_l2_mac_fn mac;
  pm_metal_net_ip_l2_tx_fn tx;
  pm_metal_net_ip_l2_poll_fn poll;
};

void pm_metal_net_ip_poll(void);
uint32_t pm_metal_net_ip_if_count(void);
int32_t pm_metal_net_ip_if_status_index(uint32_t index, char * dest, uint32_t dest_cap);
int32_t pm_metal_net_ip_l2_start(const char * backend, const pm_metal_net_ip_l2_ops_t * ops);
int32_t pm_metal_net_ip_loopback_start(void);
int32_t pm_metal_net_ip_if_dhcp_ready(const char * ifname, char * ip_out, uint32_t ip_cap);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_PYMERGETIC_METAL_NET_IP_H_ */
