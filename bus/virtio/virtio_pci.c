#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/bus/pci.h"
#include "pymergetic/metal/bus/virtio.h"
#include "pymergetic/metal/mem.h"

#define PM_ST_OK           0
#define PM_ST_UNSUPPORTED  (-1)
#define PM_ST_NOT_FOUND    (-3)
#define PM_ST_FAILED(s)    ((s) != 0)

#define VIRTIO_PCI_CAP_ID_VENDOR     0x09u
#define PCI_CAPBILITY_POINTER_OFFSET 0x34u
#define VIRTIO_PCI_CAP_COMMON_CFG    1u
#define VIRTIO_PCI_CAP_NOTIFY_CFG    2u
#define VIRTIO_PCI_CAP_DEVICE_CFG    4u

#pragma pack(1)
typedef struct {
    uint8_t cap_id;
    uint8_t next;
    uint8_t cap_len;
    uint8_t config_type;
    uint8_t bar;
    uint8_t pad[3];
    uint32_t offset;
    uint32_t length;
} metal_virtio_pci_cap_t;

typedef struct {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t device_status;
    uint8_t config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_avail;
    uint64_t queue_used;
} metal_virtio_common_cfg_t;
#pragma pack()

typedef struct {
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint8_t common_bar;
    uint32_t common_off;
    uint8_t notify_bar;
    uint32_t notify_off;
    uint32_t notify_mult;
    uint8_t device_bar;
    uint32_t device_off;
    uint32_t device_len;
    uint8_t *common_base;
    uint8_t *notify_base;
    uint8_t *device_base;
    uint64_t features;
    uint16_t pci_device_id;
} metal_vdev_priv_t;

static inline void pm_metal_mem_fence(void)
{
    __asm__ volatile("mfence" ::: "memory");
}

static metal_vdev_priv_t *virtio_priv(pm_metal_virtio_dev_t *dev)
{
    return (metal_vdev_priv_t *)(uintptr_t)dev->pci_io;
}

static int cfg_rd32(metal_vdev_priv_t *p, uint32_t off, uint32_t *val)
{
    if (p->common_base == NULL) {
        return PM_ST_UNSUPPORTED;
    }
    *val = *(volatile uint32_t *)(p->common_base + off);
    return PM_ST_OK;
}

static int cfg_wr32(metal_vdev_priv_t *p, uint32_t off, uint32_t val)
{
    if (p->common_base == NULL) {
        return PM_ST_UNSUPPORTED;
    }
    *(volatile uint32_t *)(p->common_base + off) = val;
    pm_metal_mem_fence();
    return PM_ST_OK;
}

static int cfg_rd8(metal_vdev_priv_t *p, uint32_t off, uint8_t *val)
{
    if (p->common_base == NULL) {
        return PM_ST_UNSUPPORTED;
    }
    *val = *(volatile uint8_t *)(p->common_base + off);
    return PM_ST_OK;
}

static int cfg_wr8(metal_vdev_priv_t *p, uint32_t off, uint8_t val)
{
    if (p->common_base == NULL) {
        return PM_ST_UNSUPPORTED;
    }
    *(volatile uint8_t *)(p->common_base + off) = val;
    pm_metal_mem_fence();
    return PM_ST_OK;
}

static int find_cap(uint8_t bus, uint8_t dev, uint8_t func, uint8_t type,
                    metal_virtio_pci_cap_t *out, uint32_t *extra32)
{
    uint8_t ptr = pm_metal_bus_pci_read8(bus, dev, func, PCI_CAPBILITY_POINTER_OFFSET);

    while (ptr >= 0x40u && ptr != 0xffu) {
        uint8_t id = pm_metal_bus_pci_read8(bus, dev, func, ptr);
        if (id == VIRTIO_PCI_CAP_ID_VENDOR) {
            metal_virtio_pci_cap_t cap;
            uint8_t buf[sizeof(cap) + 4];
            uintptr_t i;

            memset(buf, 0, sizeof(buf));
            for (i = 0; i < sizeof(buf); i++) {
                buf[i] = pm_metal_bus_pci_read8(bus, dev, func, (uint8_t)(ptr + i));
            }
            memcpy(&cap, buf, sizeof(cap));
            if (cap.config_type == type) {
                memcpy(out, &cap, sizeof(cap));
                if (extra32 != NULL) {
                    memcpy(extra32, buf + sizeof(cap), 4);
                }
                return PM_ST_OK;
            }
        }
        ptr = pm_metal_bus_pci_read8(bus, dev, func, (uint8_t)(ptr + 1u));
    }

    return PM_ST_NOT_FOUND;
}

static int try_open_bdf(uint8_t bus, uint8_t dev, uint8_t func, uint16_t want_id,
                        pm_metal_virtio_dev_t *out)
{
    metal_virtio_pci_cap_t common;
    metal_virtio_pci_cap_t notify;
    metal_virtio_pci_cap_t device;
    uint32_t notify_mult = 0;
    metal_vdev_priv_t *p;
    uint64_t bar_base;
    uint8_t consumed;
    uint16_t vendor_id;
    uint16_t device_id;

    vendor_id = pm_metal_bus_pci_read16(bus, dev, func, 0x00u);
    if (vendor_id != PM_METAL_VIRTIO_VENDOR) {
        return PM_ST_UNSUPPORTED;
    }
    device_id = pm_metal_bus_pci_read16(bus, dev, func, 0x02u);
    if (device_id != want_id) {
        return PM_ST_UNSUPPORTED;
    }

    pm_metal_bus_pci_enable_mem_bm(bus, dev, func);

    if (PM_ST_FAILED(find_cap(bus, dev, func, VIRTIO_PCI_CAP_COMMON_CFG, &common, NULL))) {
        return PM_ST_NOT_FOUND;
    }
    if (PM_ST_FAILED(find_cap(bus, dev, func, VIRTIO_PCI_CAP_NOTIFY_CFG, &notify, &notify_mult))) {
        return PM_ST_NOT_FOUND;
    }

    memset(&device, 0, sizeof(device));
    (void)find_cap(bus, dev, func, VIRTIO_PCI_CAP_DEVICE_CFG, &device, NULL);

    p = (metal_vdev_priv_t *)(uintptr_t)pm_metal_mem_alloc(sizeof(*p));
    if (p == NULL) {
        return PM_ST_UNSUPPORTED;
    }
    memset(p, 0, sizeof(*p));

    p->bus = bus;
    p->dev = dev;
    p->func = func;
    p->common_bar = common.bar;
    p->common_off = common.offset;
    p->notify_bar = notify.bar;
    p->notify_off = notify.offset;
    p->notify_mult = notify_mult ? notify_mult : 1u;
    p->device_bar = device.bar;
    p->device_off = device.offset;
    p->device_len = device.length;
    p->pci_device_id = device_id;

    bar_base = pm_metal_bus_pci_bar_mmio(bus, dev, func, common.bar, &consumed);
    if (bar_base == 0) {
        pm_metal_mem_free((uint8_t *)p);
        return PM_ST_UNSUPPORTED;
    }
    p->common_base = (uint8_t *)(uintptr_t)(bar_base + common.offset);

    bar_base = pm_metal_bus_pci_bar_mmio(bus, dev, func, notify.bar, &consumed);
    if (bar_base == 0) {
        pm_metal_mem_free((uint8_t *)p);
        return PM_ST_UNSUPPORTED;
    }
    p->notify_base = (uint8_t *)(uintptr_t)(bar_base + notify.offset);

    if (device.length != 0) {
        bar_base = pm_metal_bus_pci_bar_mmio(bus, dev, func, device.bar, &consumed);
        if (bar_base != 0) {
            p->device_base = (uint8_t *)(uintptr_t)(bar_base + device.offset);
        }
    }

    memset(out, 0, sizeof(*out));
    out->pci_io = (void *)(uintptr_t)p;
    out->pci_device_id = device_id;
    out->common = p->common_base;
    out->notify = p->notify_base;
    out->device_cfg = p->device_base;
    out->notify_off_mult = p->notify_mult;
    out->mmio = 1;

    (void)cfg_wr8(p, offsetof(metal_virtio_common_cfg_t, device_status), 0);
    (void)cfg_wr8(p,
                  offsetof(metal_virtio_common_cfg_t, device_status),
                  (uint8_t)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER));

    return PM_ST_OK;
}

int pm_metal_virtio_open(uint16_t pci_device_id, pm_metal_virtio_dev_t *out)
{
    uint8_t bus;
    uint8_t dev;
    uint8_t func;

    if (out == NULL) {
        return -1;
    }

    for (bus = 0; bus < 8u; bus++) {
        for (dev = 0; dev < 32u; dev++) {
            uint8_t fmax;
            uint8_t hdr;

            if (pm_metal_bus_pci_read16(bus, dev, 0, 0x00u) == 0xffffu) {
                continue;
            }
            hdr = pm_metal_bus_pci_read8(bus, dev, 0, 0x0eu);
            fmax = (hdr & 0x80u) ? 8u : 1u;
            for (func = 0; func < fmax; func++) {
                if (try_open_bdf(bus, dev, func, pci_device_id, out) == PM_ST_OK) {
                    return 0;
                }
            }
        }
    }

    return -1;
}

void pm_metal_virtio_close(pm_metal_virtio_dev_t *dev)
{
    metal_vdev_priv_t *p;

    if (dev == NULL || dev->pci_io == NULL) {
        return;
    }

    p = virtio_priv(dev);
    (void)cfg_wr8(p, offsetof(metal_virtio_common_cfg_t, device_status), 0);
    pm_metal_mem_free((uint8_t *)p);
    memset(dev, 0, sizeof(*dev));
}

uint64_t pm_metal_virtio_get_features(pm_metal_virtio_dev_t *dev)
{
    metal_vdev_priv_t *p;
    uint32_t lo;
    uint32_t hi;

    if (dev == NULL || dev->pci_io == NULL) {
        return 0;
    }

    p = virtio_priv(dev);
    (void)cfg_wr32(p, offsetof(metal_virtio_common_cfg_t, device_feature_select), 0);
    (void)cfg_rd32(p, offsetof(metal_virtio_common_cfg_t, device_feature), &lo);
    (void)cfg_wr32(p, offsetof(metal_virtio_common_cfg_t, device_feature_select), 1);
    (void)cfg_rd32(p, offsetof(metal_virtio_common_cfg_t, device_feature), &hi);
    return ((uint64_t)hi << 32) | lo;
}

int pm_metal_virtio_set_features(pm_metal_virtio_dev_t *dev, uint64_t features)
{
    metal_vdev_priv_t *p;
    uint8_t st;

    if (dev == NULL || dev->pci_io == NULL) {
        return -1;
    }

    p = virtio_priv(dev);
    (void)cfg_wr32(p, offsetof(metal_virtio_common_cfg_t, driver_feature_select), 0);
    (void)cfg_wr32(p, offsetof(metal_virtio_common_cfg_t, driver_feature), (uint32_t)features);
    (void)cfg_wr32(p, offsetof(metal_virtio_common_cfg_t, driver_feature_select), 1);
    (void)cfg_wr32(p, offsetof(metal_virtio_common_cfg_t, driver_feature), (uint32_t)(features >> 32));

    (void)cfg_rd8(p, offsetof(metal_virtio_common_cfg_t, device_status), &st);
    st |= PM_METAL_VIRTIO_S_FEATURES;
    (void)cfg_wr8(p, offsetof(metal_virtio_common_cfg_t, device_status), st);
    (void)cfg_rd8(p, offsetof(metal_virtio_common_cfg_t, device_status), &st);
    if ((st & PM_METAL_VIRTIO_S_FEATURES) == 0u) {
        return -1;
    }

    p->features = features;
    dev->features = features;
    return 0;
}

void pm_metal_virtio_set_status(pm_metal_virtio_dev_t *dev, uint8_t status)
{
    if (dev == NULL || dev->pci_io == NULL) {
        return;
    }
    (void)cfg_wr8(virtio_priv(dev), offsetof(metal_virtio_common_cfg_t, device_status), status);
}

uint8_t pm_metal_virtio_get_status(pm_metal_virtio_dev_t *dev)
{
    uint8_t st = 0;

    if (dev == NULL || dev->pci_io == NULL) {
        return 0;
    }
    (void)cfg_rd8(virtio_priv(dev), offsetof(metal_virtio_common_cfg_t, device_status), &st);
    return st;
}

int pm_metal_virtio_cfg_read(pm_metal_virtio_dev_t *dev, uint32_t offset, void *buf, uint32_t len)
{
    metal_vdev_priv_t *p;
    uint32_t i;

    if (dev == NULL || buf == NULL || len == 0 || dev->pci_io == NULL) {
        return -1;
    }

    p = virtio_priv(dev);
    if (p->device_len == 0 || offset + len > p->device_len) {
        return -1;
    }
    if (p->device_base == NULL) {
        return -1;
    }

    for (i = 0; i < len; i++) {
        ((uint8_t *)buf)[i] = *(volatile uint8_t *)(p->device_base + offset + i);
    }
    return 0;
}
