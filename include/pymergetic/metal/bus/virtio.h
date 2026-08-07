#ifndef PM_METAL_BUS_VIRTIO_H_
#define PM_METAL_BUS_VIRTIO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_MEM_PAGE_SIZE             4096u
#define PM_METAL_VIRTIO_SIZE_TO_PAGES(sz)  (((uint32_t)(sz) + 4095u) / 4096u)

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
typedef struct pm_metal_virtq pm_metal_virtq_t;

struct pm_metal_virtq {
    uint16_t qidx;
    uint16_t size;
    uint16_t free_head;
    uint16_t num_free;
    uint16_t last_used;
    uint16_t notify_off;
    void *desc;
    void *avail;
    void *used;
    void *ring_mem;
    uint32_t ring_pages;
    uint64_t desc_phys;
    uint64_t avail_phys;
    uint64_t used_phys;
    uint16_t *next;
};

struct pm_metal_virtio_dev {
    void *pci_io;
    uint16_t pci_device_id;
    uint8_t *common;
    uint8_t *notify;
    uint8_t *device_cfg;
    uint32_t notify_off_mult;
    uint64_t features;
    pm_metal_virtq_t *vqs;
    uint16_t n_vqs;
    int mmio;
};

void *pm_metal_virtio_pages_alloc(unsigned pages);
void pm_metal_virtio_pages_free(void *buf, unsigned pages);

int pm_metal_virtio_open(uint16_t pci_device_id, pm_metal_virtio_dev_t *out);
void pm_metal_virtio_close(pm_metal_virtio_dev_t *dev);
uint64_t pm_metal_virtio_get_features(pm_metal_virtio_dev_t *dev);
int pm_metal_virtio_set_features(pm_metal_virtio_dev_t *dev, uint64_t features);
void pm_metal_virtio_set_status(pm_metal_virtio_dev_t *dev, uint8_t status);
uint8_t pm_metal_virtio_get_status(pm_metal_virtio_dev_t *dev);
int pm_metal_virtio_cfg_read(pm_metal_virtio_dev_t *dev, uint32_t offset, void *buf, uint32_t len);
int pm_metal_virtio_setup_queue(pm_metal_virtio_dev_t *dev, uint16_t qidx, uint16_t want_size);
int pm_metal_virtio_driver_ok(pm_metal_virtio_dev_t *dev);
int pm_metal_virtq_add(pm_metal_virtq_t *vq, void *buf, uint32_t len, int device_writeable,
                       uint16_t *head_out);
void pm_metal_virtq_kick(pm_metal_virtio_dev_t *dev, pm_metal_virtq_t *vq);
int pm_metal_virtq_get_used(pm_metal_virtq_t *vq, uint16_t *head, uint32_t *len);
void pm_metal_virtq_free_chain(pm_metal_virtq_t *vq, uint16_t head);

#ifdef __cplusplus
}
#endif

#endif
