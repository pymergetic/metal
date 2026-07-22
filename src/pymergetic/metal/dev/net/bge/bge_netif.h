/** @file Broadcom bge L2 netif (FreeBSD-derived, BSD-4-Clause). */
#ifndef PYMERGETIC_METAL_DEV_NET_BGE_BGE_NETIF_H_
#define PYMERGETIC_METAL_DEV_NET_BGE_BGE_NETIF_H_

#include <stdint.h>

typedef void (*pm_metal_bge_netif_rx_fn)(void *ctx, const uint8_t *frame,
                                         uint32_t len);

/** Returns 0 when a supported BCM57xx NIC is present. */
int pm_metal_bge_netif_detect(void);

int pm_metal_bge_netif_open(uint8_t mac_out[6]);

int pm_metal_bge_netif_ready(void);

const uint8_t *pm_metal_bge_netif_mac(void);

int pm_metal_bge_netif_tx(const void *frame, uint32_t len);

void pm_metal_bge_netif_poll(pm_metal_bge_netif_rx_fn on_frame, void *ctx);

#endif /* PYMERGETIC_METAL_DEV_NET_BGE_BGE_NETIF_H_ */
