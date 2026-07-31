/*
 * bus/virtio — transport + DT detect (host-only).
 * Impl: _pci.c (queues/MMIO), _detect.c (inventory).
 */
#ifndef PYMERGETIC_METAL_BUS_VIRTIO_H_
#define PYMERGETIC_METAL_BUS_VIRTIO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

#define PM_METAL_VIRTIO_VENDOR             0x1AF4u
#define PM_METAL_VIRTIO_DEV_NET_LEGACY     0x1000u
#define PM_METAL_VIRTIO_DEV_BLK_LEGACY     0x1001u
#define PM_METAL_VIRTIO_DEV_CONSOLE_LEGACY 0x1003u
#define PM_METAL_VIRTIO_DEV_NET            0x1041u
#define PM_METAL_VIRTIO_DEV_BLK            0x1042u
#define PM_METAL_VIRTIO_DEV_CONSOLE        0x1043u
#define PM_METAL_VIRTIO_DEV_GPU            0x1050u
#define PM_METAL_VIRTIO_DEV_INPUT          0x1052u
#define PM_METAL_VIRTIO_DEV_SOUND          0x1059u

#define PM_METAL_VIRTIO_S_ACK       1u
#define PM_METAL_VIRTIO_S_DRIVER    2u
#define PM_METAL_VIRTIO_S_DRIVER_OK 4u
#define PM_METAL_VIRTIO_S_FEATURES  8u
#define PM_METAL_VIRTIO_S_FAILED    128u

#define PM_METAL_VIRTIO_F_VERSION_1 (1ull << 32)

typedef struct pm_metal_virtio_dev pm_metal_virtio_dev_t;
typedef struct pm_metal_virtq      pm_metal_virtq_t;

struct pm_metal_virtq {
  uint16_t  qidx;
  uint16_t  size;
  uint16_t  free_head;
  uint16_t  num_free;
  uint16_t  last_used;
  uint16_t  notify_off;
  void     *desc;
  void     *avail;
  void     *used;
  void     *ring_mem;
  uint32_t  ring_pages;
  uint64_t  desc_phys;
  uint64_t  avail_phys;
  uint64_t  used_phys;
  uint16_t *next;
};

struct pm_metal_virtio_dev {
  void             *pci_io;
  void             *handle;
  uint16_t          pci_device_id;
  uint8_t          *common;
  uint8_t          *notify;
  uint8_t          *device_cfg;
  uint32_t          notify_off_mult;
  uint32_t          common_bar;
  uint32_t          notify_bar;
  uint32_t          device_bar;
  uint64_t          features;
  pm_metal_virtq_t *vqs;
  uint16_t          n_vqs;
  int               mmio;
};

void *pm_metal_virtio_pages_alloc(unsigned pages);
void  pm_metal_virtio_pages_free(void *buf, unsigned pages);

#define PM_METAL_VIRTIO_SIZE_TO_PAGES(sz) (((sz) + 4095u) / 4096u)

int pm_metal_virtio_queue_reset(pm_metal_virtio_dev_t *dev, uint16_t qidx);
int pm_metal_virtio_post_ebs_resume(pm_metal_virtio_dev_t *dev, uint16_t qidx);

int pm_metal_virtio_cfg_write(pm_metal_virtio_dev_t *dev,
                              uint32_t               offset,
                              const void            *buf,
                              uint32_t               len);

int pm_metal_virtio_open(uint16_t pci_device_id, pm_metal_virtio_dev_t *out);
int pm_metal_virtio_find(uint16_t pci_device_id);

void pm_metal_virtio_close(pm_metal_virtio_dev_t *dev);

uint64_t pm_metal_virtio_get_features(pm_metal_virtio_dev_t *dev);
int      pm_metal_virtio_set_features(pm_metal_virtio_dev_t *dev, uint64_t features);

int pm_metal_virtio_setup_queue(pm_metal_virtio_dev_t *dev, uint16_t qidx, uint16_t want_size);

void    pm_metal_virtio_set_status(pm_metal_virtio_dev_t *dev, uint8_t status);
uint8_t pm_metal_virtio_get_status(pm_metal_virtio_dev_t *dev);
int     pm_metal_virtio_driver_ok(pm_metal_virtio_dev_t *dev);

int pm_metal_virtq_add(
  pm_metal_virtq_t *vq, void *buf, uint32_t len, int device_writeable, uint16_t *head_out);

int pm_metal_virtq_add2(pm_metal_virtq_t *vq,
                        void             *buf0,
                        uint32_t          len0,
                        int               write0,
                        void             *buf1,
                        uint32_t          len1,
                        int               write1,
                        uint16_t         *head_out);

int pm_metal_virtq_add3(pm_metal_virtq_t *vq,
                        void             *buf0,
                        uint32_t          len0,
                        int               write0,
                        void             *buf1,
                        uint32_t          len1,
                        int               write1,
                        void             *buf2,
                        uint32_t          len2,
                        int               write2,
                        uint16_t         *head_out);

void pm_metal_virtq_kick(pm_metal_virtio_dev_t *dev, pm_metal_virtq_t *vq);

int pm_metal_virtq_get_used(pm_metal_virtq_t *vq, uint16_t *head, uint32_t *len);

void pm_metal_virtq_free_chain(pm_metal_virtq_t *vq, uint16_t head);

int pm_metal_virtio_cfg_read(pm_metal_virtio_dev_t *dev, uint32_t offset, void *buf, uint32_t len);

void pm_metal_virtio_ack_isr(pm_metal_virtio_dev_t *dev);

/** Scan PCI for virtio devices; add DT nodes (inventory only). */
int32_t pm_metal_bus_virtio_detect(void);

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BUS_VIRTIO_H_ */
