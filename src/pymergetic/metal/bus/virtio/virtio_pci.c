#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/bus/pci.h"
#include "pymergetic/metal/bus/virtio.h"
#include "pymergetic/metal/mem.h"

#include <pymergetic/metal/reg/mod.h>

/* RegMod declare (C SoT) — loaded via pm_metal_bus_virtio_reg_load. */
static pm_metal_reg_export_t bus_virtio_exports[] = {
    PM_METAL_REG_EXPORT(open),
    PM_METAL_REG_EXPORT(set_features),
    PM_METAL_REG_EXPORT(cfg_read),
    PM_METAL_REG_EXPORT(setup_queue),
    PM_METAL_REG_EXPORT(driver_ok),
};
PM_METAL_REG_REF(bus_virtio, open, 0);
PM_METAL_REG_REF(bus_virtio, set_features, 1);
PM_METAL_REG_REF(bus_virtio, cfg_read, 2);
PM_METAL_REG_REF(bus_virtio, setup_queue, 3);
PM_METAL_REG_REF(bus_virtio, driver_ok, 4);
PM_METAL_REG_MOD(bus_virtio, "pymergetic.metal.bus.virtio")

static int32_t bus_virtio_register_symbols(void *ctx)
{
    (void)ctx;
    pm_metal_reg_export_publish(bus_virtio_open, (void *)pm_metal_virtio_open);
    pm_metal_reg_export_publish(bus_virtio_set_features, (void *)pm_metal_virtio_set_features);
    pm_metal_reg_export_publish(bus_virtio_cfg_read, (void *)pm_metal_virtio_cfg_read);
    pm_metal_reg_export_publish(bus_virtio_setup_queue, (void *)pm_metal_virtio_setup_queue);
    pm_metal_reg_export_publish(bus_virtio_driver_ok, (void *)pm_metal_virtio_driver_ok);
    return 0;
}

#define PM_ST_OK           0
#define PM_ST_UNSUPPORTED  (-1)
#define PM_ST_NOT_FOUND    (-3)
#define PM_ST_FAILED(s)    ((s) != 0)

#define VIRTIO_PCI_CAP_ID_VENDOR     0x09u
#define PCI_CAPBILITY_POINTER_OFFSET 0x34u
#define VIRTIO_PCI_CAP_COMMON_CFG    1u
#define VIRTIO_PCI_CAP_NOTIFY_CFG    2u
#define VIRTIO_PCI_CAP_DEVICE_CFG    4u

#define VRING_DESC_F_NEXT  1u
#define VRING_DESC_F_WRITE 2u

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

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} metal_vring_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} metal_vring_avail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} metal_vring_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    metal_vring_used_elem_t ring[];
} metal_vring_used_t;
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
    pm_metal_virtq_t *vqs;
    uint16_t n_vqs;
} metal_vdev_priv_t;

static inline void pm_metal_mem_fence(void)
{
    __asm__ volatile("mfence" ::: "memory");
}

static metal_vdev_priv_t *virtio_priv(pm_metal_virtio_dev_t *dev)
{
    return (metal_vdev_priv_t *)(uintptr_t)dev->pci_io;
}

void *pm_metal_virtio_pages_alloc(unsigned pages)
{
    if (pages == 0) {
        return NULL;
    }
    return pm_metal_mem_memalign(PM_METAL_MEM_PAGE_SIZE, (size_t)pages * PM_METAL_MEM_PAGE_SIZE);
}

void pm_metal_virtio_pages_free(void *buf, unsigned pages)
{
    (void)pages;
    if (buf != NULL) {
        pm_metal_mem_free(buf);
    }
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

static int cfg_rd16(metal_vdev_priv_t *p, uint32_t off, uint16_t *val)
{
    if (p->common_base == NULL) {
        return PM_ST_UNSUPPORTED;
    }
    *val = *(volatile uint16_t *)(p->common_base + off);
    return PM_ST_OK;
}

static int cfg_wr16(metal_vdev_priv_t *p, uint32_t off, uint16_t val)
{
    if (p->common_base == NULL) {
        return PM_ST_UNSUPPORTED;
    }
    *(volatile uint16_t *)(p->common_base + off) = val;
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

static int cfg_wr64(metal_vdev_priv_t *p, uint32_t off, uint64_t val)
{
    if (PM_ST_FAILED(cfg_wr32(p, off, (uint32_t)val))) {
        return PM_ST_UNSUPPORTED;
    }
    return cfg_wr32(p, off + 4u, (uint32_t)(val >> 32));
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
    uint16_t i;

    if (dev == NULL || dev->pci_io == NULL) {
        return;
    }

    p = virtio_priv(dev);
    (void)cfg_wr8(p, offsetof(metal_virtio_common_cfg_t, device_status), 0);

    if (p->vqs != NULL) {
        for (i = 0; i < p->n_vqs; i++) {
            if (p->vqs[i].ring_mem != NULL) {
                pm_metal_virtio_pages_free(p->vqs[i].ring_mem, p->vqs[i].ring_pages);
            }
            if (p->vqs[i].next != NULL) {
                pm_metal_mem_free((uint8_t *)p->vqs[i].next);
            }
        }
        pm_metal_mem_free((uint8_t *)p->vqs);
    }

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

int pm_metal_virtio_driver_ok(pm_metal_virtio_dev_t *dev)
{
    uint8_t st;

    st = pm_metal_virtio_get_status(dev);
    st |= PM_METAL_VIRTIO_S_DRIVER_OK;
    pm_metal_virtio_set_status(dev, st);
    return 0;
}

int pm_metal_virtio_setup_queue(pm_metal_virtio_dev_t *dev, uint16_t qidx, uint16_t want_size)
{
    metal_vdev_priv_t *p;
    pm_metal_virtq_t *vq;
    uint16_t qsz;
    uintptr_t desc_bytes;
    uintptr_t avail_bytes;
    uintptr_t used_bytes;
    uintptr_t total;
    uintptr_t pages;
    uint8_t *mem;
    uint16_t i;

    if (dev == NULL || dev->pci_io == NULL) {
        return -1;
    }

    p = virtio_priv(dev);
    (void)cfg_wr16(p, offsetof(metal_virtio_common_cfg_t, queue_select), qidx);
    qsz = 0;
    (void)cfg_rd16(p, offsetof(metal_virtio_common_cfg_t, queue_size), &qsz);
    if (qsz == 0) {
        return -1;
    }
    if (want_size > 0 && want_size < qsz) {
        qsz = want_size;
    }

    if (p->vqs == NULL) {
        p->vqs = (pm_metal_virtq_t *)pm_metal_mem_alloc(sizeof(pm_metal_virtq_t) * 8u);
        if (p->vqs == NULL) {
            return -1;
        }
        memset(p->vqs, 0, sizeof(pm_metal_virtq_t) * 8u);
        p->n_vqs = 8;
        dev->vqs = p->vqs;
        dev->n_vqs = 8;
    }

    if (qidx >= p->n_vqs) {
        return -1;
    }

    vq = &p->vqs[qidx];
    desc_bytes = sizeof(metal_vring_desc_t) * qsz;
    avail_bytes = sizeof(uint16_t) * (3u + qsz);
    used_bytes = sizeof(uint16_t) * 3u + sizeof(metal_vring_used_elem_t) * qsz;
    total = desc_bytes + avail_bytes + 4096u + used_bytes;
    pages = PM_METAL_VIRTIO_SIZE_TO_PAGES(total);
    mem = pm_metal_virtio_pages_alloc((unsigned)pages);
    if (mem == NULL) {
        return -1;
    }

    memset(mem, 0, pages * PM_METAL_MEM_PAGE_SIZE);
    vq->qidx = qidx;
    vq->size = qsz;
    vq->ring_mem = mem;
    vq->ring_pages = (uint32_t)pages;
    vq->desc = mem;
    vq->avail = mem + desc_bytes;
    vq->used = mem + ((desc_bytes + avail_bytes + 4095u) & ~4095u);
    vq->desc_phys = (uint64_t)(uintptr_t)vq->desc;
    vq->avail_phys = (uint64_t)(uintptr_t)vq->avail;
    vq->used_phys = (uint64_t)(uintptr_t)vq->used;
    vq->free_head = 0;
    vq->num_free = qsz;
    vq->last_used = 0;
    vq->next = (uint16_t *)pm_metal_mem_alloc(sizeof(uint16_t) * qsz);
    if (vq->next == NULL) {
        pm_metal_virtio_pages_free(mem, (unsigned)pages);
        return -1;
    }

    for (i = 0; i < qsz - 1u; i++) {
        vq->next[i] = (uint16_t)(i + 1u);
    }
    vq->next[qsz - 1u] = 0xffffu;

    (void)cfg_wr16(p, offsetof(metal_virtio_common_cfg_t, queue_select), qidx);
    (void)cfg_wr16(p, offsetof(metal_virtio_common_cfg_t, queue_size), qsz);
    (void)cfg_wr64(p, offsetof(metal_virtio_common_cfg_t, queue_desc), vq->desc_phys);
    (void)cfg_wr64(p, offsetof(metal_virtio_common_cfg_t, queue_avail), vq->avail_phys);
    (void)cfg_wr64(p, offsetof(metal_virtio_common_cfg_t, queue_used), vq->used_phys);
    (void)cfg_rd16(p, offsetof(metal_virtio_common_cfg_t, queue_notify_off), &vq->notify_off);
    (void)cfg_wr16(p, offsetof(metal_virtio_common_cfg_t, queue_enable), 1);
    return 0;
}

int pm_metal_virtq_add(pm_metal_virtq_t *vq, void *buf, uint32_t len, int device_writeable,
                       uint16_t *head_out)
{
    metal_vring_desc_t *desc;
    metal_vring_avail_t *avail;
    uint16_t head;
    uint16_t aidx;

    if (vq == NULL || buf == NULL || len == 0 || vq->num_free == 0) {
        return -1;
    }

    head = vq->free_head;
    vq->free_head = vq->next[head];
    vq->num_free--;

    desc = (metal_vring_desc_t *)vq->desc;
    desc[head].addr = (uint64_t)(uintptr_t)buf;
    desc[head].len = len;
    desc[head].flags = (uint16_t)(device_writeable ? VRING_DESC_F_WRITE : 0);
    desc[head].next = 0;

    avail = (metal_vring_avail_t *)vq->avail;
    aidx = avail->idx;
    pm_metal_mem_fence();
    avail->ring[aidx % vq->size] = head;
    pm_metal_mem_fence();
    avail->idx = (uint16_t)(aidx + 1u);

    if (head_out != NULL) {
        *head_out = head;
    }
    return 0;
}

void pm_metal_virtq_kick(pm_metal_virtio_dev_t *dev, pm_metal_virtq_t *vq)
{
    metal_vdev_priv_t *p;
    uint32_t off;

    if (dev == NULL || vq == NULL || dev->pci_io == NULL) {
        return;
    }

    p = virtio_priv(dev);
    off = vq->notify_off * p->notify_mult;
    if (p->notify_base != NULL) {
        *(volatile uint16_t *)(p->notify_base + off) = vq->qidx;
        pm_metal_mem_fence();
    }
}

int pm_metal_virtq_get_used(pm_metal_virtq_t *vq, uint16_t *head, uint32_t *len)
{
    metal_vring_used_t *used;
    uint16_t uidx;

    if (vq == NULL) {
        return 0;
    }

    used = (metal_vring_used_t *)vq->used;
    pm_metal_mem_fence();
    uidx = used->idx;
    if (uidx == vq->last_used) {
        return 0;
    }

    if (head != NULL) {
        *head = (uint16_t)used->ring[vq->last_used % vq->size].id;
    }
    if (len != NULL) {
        *len = used->ring[vq->last_used % vq->size].len;
    }

    vq->last_used = (uint16_t)(vq->last_used + 1u);
    return 1;
}

void pm_metal_virtq_free_chain(pm_metal_virtq_t *vq, uint16_t head)
{
    metal_vring_desc_t *desc;
    uint16_t cur;
    uint16_t next;

    if (vq == NULL) {
        return;
    }

    desc = (metal_vring_desc_t *)vq->desc;
    cur = head;
    for (;;) {
        next = desc[cur].next;
        vq->next[cur] = vq->free_head;
        vq->free_head = cur;
        vq->num_free++;
        if ((desc[cur].flags & VRING_DESC_F_NEXT) == 0) {
            break;
        }
        cur = next;
    }
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

int pm_metal_virtq_add3(pm_metal_virtq_t *vq, void *buf0, uint32_t len0, int write0, void *buf1,
                        uint32_t len1, int write1, void *buf2, uint32_t len2, int write2,
                        uint16_t *head_out)
{
    metal_vring_desc_t *desc;
    metal_vring_avail_t *avail;
    uint16_t a, b, c, aidx;

    if (vq == NULL || buf0 == NULL || buf1 == NULL || buf2 == NULL || len0 == 0 || len1 == 0 ||
        len2 == 0 || vq->num_free < 3) {
        return -1;
    }

    a = vq->free_head;
    b = vq->next[a];
    c = vq->next[b];
    vq->free_head = vq->next[c];
    vq->num_free = (uint16_t)(vq->num_free - 3u);

    desc = (metal_vring_desc_t *)vq->desc;
    desc[a].addr = (uint64_t)(uintptr_t)buf0;
    desc[a].len = len0;
    desc[a].flags = (uint16_t)(VRING_DESC_F_NEXT | (write0 ? VRING_DESC_F_WRITE : 0));
    desc[a].next = b;
    desc[b].addr = (uint64_t)(uintptr_t)buf1;
    desc[b].len = len1;
    desc[b].flags = (uint16_t)(VRING_DESC_F_NEXT | (write1 ? VRING_DESC_F_WRITE : 0));
    desc[b].next = c;
    desc[c].addr = (uint64_t)(uintptr_t)buf2;
    desc[c].len = len2;
    desc[c].flags = (uint16_t)(write2 ? VRING_DESC_F_WRITE : 0);
    desc[c].next = 0;

    avail = (metal_vring_avail_t *)vq->avail;
    aidx = avail->idx;
    pm_metal_mem_fence();
    avail->ring[aidx % vq->size] = a;
    pm_metal_mem_fence();
    avail->idx = (uint16_t)(aidx + 1u);

    if (head_out != NULL) {
        *head_out = a;
    }
    return 0;
}

void pm_metal_virtio_ack_isr(pm_metal_virtio_dev_t *dev)
{
    (void)dev;
}
