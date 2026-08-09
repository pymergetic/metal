#ifndef PYMERGETIC_METAL_DEV_NET_VIRTIO_NET_H_
#define PYMERGETIC_METAL_DEV_NET_VIRTIO_NET_H_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_dev_net_virtio_probe(void);
int32_t pm_metal_dev_net_virtio_open(void);
int32_t pm_metal_dev_net_virtio_ready(void);
int32_t pm_metal_dev_net_virtio_mac(void);
int32_t pm_metal_dev_net_virtio_tx(void);
int32_t pm_metal_dev_net_virtio_poll(void);
int32_t pm_metal_dev_net_virtio_reap_tx(void);
#ifdef __cplusplus
}
#endif
#endif /* PYMERGETIC_METAL_DEV_NET_VIRTIO_NET_H_ */
