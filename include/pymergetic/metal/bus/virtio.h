#ifndef PM_METAL_BUS_VIRTIO_H_
#define PM_METAL_BUS_VIRTIO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_VIRTIO_VENDOR             0x1af4u
#define PM_METAL_VIRTIO_DEV_NET_LEGACY     0x1000u
#define PM_METAL_VIRTIO_DEV_NET            0x1041u

#define PM_METAL_VIRTIO_S_ACK              1u
#define PM_METAL_VIRTIO_S_DRIVER           2u
#define PM_METAL_VIRTIO_S_DRIVER_OK        4u
#define PM_METAL_VIRTIO_S_FEATURES         8u
#define PM_METAL_VIRTIO_S_FAILED           128u

#define PM_METAL_VIRTIO_F_VERSION_1        (1ull << 32)
#define PM_METAL_VIRTIO_F_MAC              (1ull << 5)

typedef struct pm_metal_virtio_dev pm_metal_virtio_dev_t;

struct pm_metal_virtio_dev {
    void *pci_io;
    uint16_t pci_device_id;
    uint8_t *common;
    uint8_t *notify;
    uint8_t *device_cfg;
    uint32_t notify_off_mult;
    uint64_t features;
    int mmio;
};

int pm_metal_virtio_open(uint16_t pci_device_id, pm_metal_virtio_dev_t *out);
void pm_metal_virtio_close(pm_metal_virtio_dev_t *dev);
uint64_t pm_metal_virtio_get_features(pm_metal_virtio_dev_t *dev);
int pm_metal_virtio_set_features(pm_metal_virtio_dev_t *dev, uint64_t features);
void pm_metal_virtio_set_status(pm_metal_virtio_dev_t *dev, uint8_t status);
uint8_t pm_metal_virtio_get_status(pm_metal_virtio_dev_t *dev);
int pm_metal_virtio_cfg_read(pm_metal_virtio_dev_t *dev, uint32_t offset, void *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif
