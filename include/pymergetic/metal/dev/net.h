#ifndef PM_METAL_DEV_NET_H_
#define PM_METAL_DEV_NET_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Probe virtio-net PCI: map caps, negotiate MAC feature, read MAC if present. */
int pm_metal_dev_net_virtio_probe(uint8_t mac_out[6]);

#ifdef __cplusplus
}
#endif

#endif
