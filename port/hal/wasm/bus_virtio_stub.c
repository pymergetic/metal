/*
 * Browser bus.virtio — same C ABI; no PCI virtio devices in the browser seat.
 */
#include "pymergetic/metal/bus/virtio.h"

#include <stddef.h>
#include <string.h>

void *pm_metal_virtio_pages_alloc(unsigned pages)
{
    (void)pages;
    return NULL;
}

void pm_metal_virtio_pages_free(void *buf, unsigned pages)
{
    (void)buf;
    (void)pages;
}

int pm_metal_virtio_open(uint16_t pci_device_id, pm_metal_virtio_dev_t *out)
{
    (void)pci_device_id;
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    return -1;
}

void pm_metal_virtio_close(pm_metal_virtio_dev_t *dev) { (void)dev; }

uint64_t pm_metal_virtio_get_features(pm_metal_virtio_dev_t *dev)
{
    (void)dev;
    return 0;
}

int pm_metal_virtio_set_features(pm_metal_virtio_dev_t *dev, uint64_t features)
{
    (void)dev;
    (void)features;
    return -1;
}

void pm_metal_virtio_set_status(pm_metal_virtio_dev_t *dev, uint8_t status)
{
    (void)dev;
    (void)status;
}

uint8_t pm_metal_virtio_get_status(pm_metal_virtio_dev_t *dev)
{
    (void)dev;
    return 0;
}

int pm_metal_virtio_cfg_read(pm_metal_virtio_dev_t *dev, uint32_t offset, void *buf, uint32_t len)
{
    (void)dev;
    (void)offset;
    if (buf && len) {
        memset(buf, 0, len);
    }
    return -1;
}

int pm_metal_virtio_setup_queue(pm_metal_virtio_dev_t *dev, uint16_t qidx, uint16_t want_size)
{
    (void)dev;
    (void)qidx;
    (void)want_size;
    return -1;
}

int pm_metal_virtio_driver_ok(pm_metal_virtio_dev_t *dev)
{
    (void)dev;
    return -1;
}

int pm_metal_virtq_add(pm_metal_virtq_t *vq, void *buf, uint32_t len, int device_writeable,
                       uint16_t *head_out)
{
    (void)vq;
    (void)buf;
    (void)len;
    (void)device_writeable;
    if (head_out) {
        *head_out = 0;
    }
    return -1;
}

void pm_metal_virtq_kick(pm_metal_virtio_dev_t *dev, pm_metal_virtq_t *vq)
{
    (void)dev;
    (void)vq;
}

int pm_metal_virtq_get_used(pm_metal_virtq_t *vq, uint16_t *head, uint32_t *len)
{
    (void)vq;
    if (head) {
        *head = 0;
    }
    if (len) {
        *len = 0;
    }
    return -1;
}

void pm_metal_virtq_free_chain(pm_metal_virtq_t *vq, uint16_t head)
{
    (void)vq;
    (void)head;
}
