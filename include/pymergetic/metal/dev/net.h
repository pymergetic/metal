#ifndef PM_METAL_DEV_NET_H_
#define PM_METAL_DEV_NET_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pm_metal_dev_net_virtio_rx_fn)(void *ctx, const uint8_t *frame, uint32_t len);

/* Lightweight probe: caps + MAC only (no queues). */
int pm_metal_dev_net_virtio_probe(uint8_t mac_out[6]);

int pm_metal_dev_net_virtio_open(uint8_t mac_out[6]);
int pm_metal_dev_net_virtio_ready(void);
const uint8_t *pm_metal_dev_net_virtio_mac(void);
int pm_metal_dev_net_virtio_tx(const void *frame, uint32_t len);
void pm_metal_dev_net_virtio_poll(pm_metal_dev_net_virtio_rx_fn on_frame, void *ctx);
/* Reap completed TX descriptors; returns 1 if any completed. */
int pm_metal_dev_net_virtio_reap_tx(void);

#ifdef __cplusplus
}
#endif

#endif
