/*
 * Virtio-net L2 driver (host). Frames only — IP stack is lwIP.
 */
#ifndef PYMERGETIC_METAL_VIRTIO_NETIF_H_
#define PYMERGETIC_METAL_VIRTIO_NETIF_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pm_metal_virtio_netif_rx_fn)(void *ctx, const uint8_t *frame,
					     uint32_t len);

/** Open virtio-net queues; copy MAC to mac_out[6]. Returns 0 on success. */
int pm_metal_virtio_netif_open(uint8_t mac_out[6]);

int pm_metal_virtio_netif_ready(void);

const uint8_t *pm_metal_virtio_netif_mac(void);

/** Transmit one Ethernet frame (no virtio-net hdr — driver prepends). */
int pm_metal_virtio_netif_tx(const void *frame, uint32_t len);

/** Drain RX/TX rings; invoke on_frame for each Ethernet frame. */
void pm_metal_virtio_netif_poll(pm_metal_virtio_netif_rx_fn on_frame, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_VIRTIO_NETIF_H_ */
