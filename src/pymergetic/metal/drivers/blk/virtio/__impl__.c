/* pymergetic.metal.drivers.blk.virtio — ram disk on host via probe(nsec); PCI virtio-blk on firmware. */
#include "pymergetic/metal/drivers/blk/virtio/__exports__.h"

#include "pymergetic/metal/bus/pci.h"
#include "pymergetic/metal/bus/virtio.h"
#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/blk.h"
#include "pymergetic/util/mem.h"

#if defined(PM_METAL_FIRMWARE)
#include "pm_cpu.h"
#endif

#include <string.h>

#define VIRTIO_BLK_MAX 4u
#define VIRTIO_BLK_SEC 512u

struct virtio_blk {
    uint32_t used;
    uint32_t nsec;
    uint8_t *data;
    int32_t dt_id;
    int32_t blk_h;
    pm_metal_blk_ops_t ops;
#if defined(PM_METAL_FIRMWARE)
    volatile uint8_t *common;
    volatile uint8_t *notify;
    uint32_t notify_mult;
    uint16_t nqoff;
    uint16_t last_used;
    uint8_t *vqmem;
    uint8_t *req;
    uint8_t *stbyte;
    uint64_t cap;
#endif
};

static pm_util_mem_arena_t *s_arena;
static struct virtio_blk s_dev[VIRTIO_BLK_MAX];

#if defined(PM_METAL_FIRMWARE)
#define FW_BLK_QSZ 16u
#define FW_DESC_F_NEXT 1u
#define FW_DESC_F_WRITE 2u
#define VIRTIO_PCI_CAP_COMMON 1u
#define VIRTIO_PCI_CAP_NOTIFY 2u
#define VIRTIO_PCI_CAP_DEVCFG 4u
#define VIRTIO_F_VERSION_1 32u
#define VIRTIO_BLK_T_IN 0u
#define VIRTIO_BLK_T_OUT 1u

static uint8_t mmio_r8(volatile uint8_t *p) {
    return *p;
}

static void mmio_w8(volatile uint8_t *p, uint8_t v) {
    *p = v;
}

static uint16_t mmio_r16(volatile uint8_t *p) {
    return *(volatile uint16_t *)p;
}

static void mmio_w16(volatile uint8_t *p, uint16_t v) {
    *(volatile uint16_t *)p = v;
}

static uint32_t mmio_r32(volatile uint8_t *p) {
    return *(volatile uint32_t *)p;
}

static void mmio_w32(volatile uint8_t *p, uint32_t v) {
    *(volatile uint32_t *)p = v;
}

static void mmio_w64(volatile uint8_t *p, uint64_t v) {
    mmio_w32(p, (uint32_t)v);
    mmio_w32(p + 4, (uint32_t)(v >> 32));
}

static uint64_t pci_bar(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t bar) {
    uint32_t lo = pm_metal_bus_pci_cfg_read32(bus, dev, fn, 0x10u + bar * 4u);
    uint32_t hi;
    if ((lo & 1u) != 0) {
        return (uint64_t)(lo & 0xfffffffcu);
    }
    if (((lo >> 1) & 3u) == 2u) {
        hi = pm_metal_bus_pci_cfg_read32(bus, dev, fn, 0x10u + (bar + 1u) * 4u);
        return ((uint64_t)hi << 32) | (uint64_t)(lo & 0xfffffff0u);
    }
    return (uint64_t)(lo & 0xfffffff0u);
}

static int32_t virtio_pci_caps(uint32_t bus, uint32_t dev, uint32_t fn, volatile uint8_t **common,
    volatile uint8_t **notify, uint32_t *notify_mult, volatile uint8_t **devcfg) {
    uint32_t status = pm_metal_bus_pci_cfg_read32(bus, dev, fn, 0x04u);
    uint32_t cap;
    *common = NULL;
    *notify = NULL;
    *devcfg = NULL;
    *notify_mult = 1;
    pm_metal_bus_pci_cfg_write32(bus, dev, fn, 0x04u, status | 0x7u);
    cap = pm_metal_bus_pci_cfg_read32(bus, dev, fn, 0x34u) & 0xffu;
    while (cap != 0 && cap != 0xffu) {
        uint32_t dw0 = pm_metal_bus_pci_cfg_read32(bus, dev, fn, cap);
        uint32_t dw1 = pm_metal_bus_pci_cfg_read32(bus, dev, fn, cap + 4u);
        uint32_t off = pm_metal_bus_pci_cfg_read32(bus, dev, fn, cap + 8u);
        uint8_t id = (uint8_t)dw0;
        uint8_t next = (uint8_t)(dw0 >> 8);
        uint8_t cfg_type;
        uint8_t bar;
        uint64_t base;
        volatile uint8_t *mmio;
        if (id != 0x09u) {
            cap = next;
            continue;
        }
        cfg_type = (uint8_t)(dw0 >> 24);
        bar = (uint8_t)dw1;
        base = pci_bar(bus, dev, fn, bar);
        if (base == 0) {
            cap = next;
            continue;
        }
        mmio = (volatile uint8_t *)(uintptr_t)base + off;
        if (cfg_type == VIRTIO_PCI_CAP_COMMON) {
            *common = mmio;
        } else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY) {
            *notify = mmio;
            *notify_mult = pm_metal_bus_pci_cfg_read32(bus, dev, fn, cap + 16u);
            if (*notify_mult == 0) {
                *notify_mult = 1;
            }
        } else if (cfg_type == VIRTIO_PCI_CAP_DEVCFG) {
            *devcfg = mmio;
        }
        cap = next;
    }
    return (*common != NULL && *notify != NULL) ? 0 : -1;
}

static void desc_set(uint8_t *de, uint64_t addr, uint32_t len, uint16_t flags, uint16_t next) {
    uint32_t i;
    for (i = 0; i < 8u; i++) {
        de[i] = (uint8_t)(addr >> (8u * i));
    }
    de[8] = (uint8_t)len;
    de[9] = (uint8_t)(len >> 8);
    de[10] = (uint8_t)(len >> 16);
    de[11] = (uint8_t)(len >> 24);
    de[12] = (uint8_t)flags;
    de[13] = (uint8_t)(flags >> 8);
    de[14] = (uint8_t)next;
    de[15] = (uint8_t)(next >> 8);
}

static int32_t fw_blk_io(struct virtio_blk *d, uint64_t lba, void *buf, const void *src, uint32_t nsec,
    int wr) {
    uint8_t *desc;
    uint8_t *avail;
    uint8_t *used;
    uint16_t aidx;
    uint16_t uidx;
    uint32_t spins;
    uint32_t bytes;
    uint64_t addr;
    if (d == NULL || d->common == NULL || d->vqmem == NULL || nsec == 0) {
        return -1;
    }
    if (lba + nsec > d->cap) {
        return -1;
    }
    bytes = nsec * VIRTIO_BLK_SEC;
    memset(d->req, 0, 16);
    d->req[0] = wr ? (uint8_t)VIRTIO_BLK_T_OUT : (uint8_t)VIRTIO_BLK_T_IN;
    d->req[8] = (uint8_t)lba;
    d->req[9] = (uint8_t)(lba >> 8);
    d->req[10] = (uint8_t)(lba >> 16);
    d->req[11] = (uint8_t)(lba >> 24);
    d->req[12] = (uint8_t)(lba >> 32);
    d->req[13] = (uint8_t)(lba >> 40);
    d->req[14] = (uint8_t)(lba >> 48);
    d->req[15] = (uint8_t)(lba >> 56);
    d->stbyte[0] = 0xff;
    if (wr) {
        addr = (uint64_t)(uintptr_t)src;
    } else {
        addr = (uint64_t)(uintptr_t)buf;
    }
    desc = d->vqmem;
    avail = d->vqmem + 256;
    used = d->vqmem + 512;
    desc_set(desc, (uint64_t)(uintptr_t)d->req, 16, FW_DESC_F_NEXT, 1);
    desc_set(desc + 16, addr, bytes, wr ? FW_DESC_F_NEXT : (uint16_t)(FW_DESC_F_NEXT | FW_DESC_F_WRITE),
        2);
    desc_set(desc + 32, (uint64_t)(uintptr_t)d->stbyte, 1, FW_DESC_F_WRITE, 0);
    aidx = (uint16_t)(avail[2] | ((uint16_t)avail[3] << 8));
    avail[4 + (aidx % FW_BLK_QSZ) * 2u] = 0;
    avail[5 + (aidx % FW_BLK_QSZ) * 2u] = 0;
    aidx++;
    avail[2] = (uint8_t)aidx;
    avail[3] = (uint8_t)(aidx >> 8);
    pm_cpu_store_fence();
    mmio_w16(d->notify + (uint32_t)d->nqoff * d->notify_mult, 0);
    for (spins = 0; spins < 10000000u; spins++) {
        pm_cpu_load_fence();
        uidx = (uint16_t)(used[2] | ((uint16_t)used[3] << 8));
        if (uidx != d->last_used) {
            d->last_used = uidx;
            return d->stbyte[0] == 0 ? 0 : -1;
        }
        pm_cpu_pause();
    }
    return -1;
}
#endif

static int32_t vb_ready(void *ctx) {
    struct virtio_blk *d = ctx;
    if (d == NULL) {
        return 0;
    }
#if defined(PM_METAL_FIRMWARE)
    if (d->common != NULL) {
        return 1;
    }
#endif
    return d->data != NULL ? 1 : 0;
}

static uint64_t vb_cap(void *ctx) {
    struct virtio_blk *d = ctx;
    if (d == NULL) {
        return 0;
    }
#if defined(PM_METAL_FIRMWARE)
    if (d->common != NULL) {
        return d->cap;
    }
#endif
    return d->nsec;
}

static int32_t vb_rw(void *ctx, uint64_t lba, void *buf, const void *src, uint32_t nsec, int wr) {
    struct virtio_blk *d = ctx;
    uint64_t off;
    uint32_t bytes;
    if (d == NULL || nsec == 0) {
        return -1;
    }
#if defined(PM_METAL_FIRMWARE)
    if (d->common != NULL) {
        return fw_blk_io(d, lba, buf, src, nsec, wr);
    }
#endif
    if (d->data == NULL) {
        return -1;
    }
    if (lba + nsec > d->nsec) {
        return -1;
    }
    off = lba * VIRTIO_BLK_SEC;
    bytes = nsec * VIRTIO_BLK_SEC;
    if (wr) {
        memcpy(d->data + off, src, bytes);
    } else {
        memcpy(buf, d->data + off, bytes);
    }
    return 0;
}

static int32_t vb_read(void *ctx, uint64_t lba, void *buf, uint32_t nsec) {
    return vb_rw(ctx, lba, buf, NULL, nsec, 0);
}

static int32_t vb_write(void *ctx, uint64_t lba, const void *buf, uint32_t nsec) {
    return vb_rw(ctx, lba, NULL, buf, nsec, 1);
}

static void vb_close(void *ctx) {
    struct virtio_blk *d = ctx;
    if (d == NULL) {
        return;
    }
    d->used = 0;
    d->data = NULL;
    d->nsec = 0;
    d->dt_id = -1;
    d->blk_h = -1;
#if defined(PM_METAL_FIRMWARE)
    d->common = NULL;
    d->notify = NULL;
    d->vqmem = NULL;
    d->req = NULL;
    d->stbyte = NULL;
    d->cap = 0;
#endif
}

int32_t pm_metal_drivers_blk_virtio_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_dev, 0, sizeof(s_dev));
    return 0;
}

void pm_metal_drivers_blk_virtio_deinit(void) {
    memset(s_dev, 0, sizeof(s_dev));
    s_arena = NULL;
}

int32_t pm_metal_drivers_blk_virtio_probe(uint32_t nsec) {
    uint32_t i;
    struct virtio_blk *d;
    size_t bytes;
    if (s_arena == NULL || nsec == 0 || nsec > 256u) {
        return -1;
    }
    for (i = 0; i < VIRTIO_BLK_MAX; i++) {
        if (s_dev[i].used) {
            continue;
        }
        d = &s_dev[i];
        memset(d, 0, sizeof(*d));
        bytes = (size_t)nsec * VIRTIO_BLK_SEC;
        d->data = pm_util_mem_alloc(s_arena, bytes);
        if (d->data == NULL) {
            return -1;
        }
        memset(d->data, 0, bytes);
        d->nsec = nsec;
        d->used = 1;
        d->ops.ready = vb_ready;
        d->ops.capacity = vb_cap;
        d->ops.read = vb_read;
        d->ops.write = vb_write;
        d->ops.close = vb_close;
        d->ops.ctx = d;
        d->dt_id = pm_metal_dt_add(PM_METAL_DT_CLASS_BLK, "virtio-blk", PM_METAL_DT_BUS_VIRTIO, 0, 0,
            0, i);
        if (d->dt_id < 0) {
            d->used = 0;
            return -1;
        }
        d->blk_h = pm_metal_drivers_blk_bind(d->dt_id, &d->ops);
        if (d->blk_h < 0) {
            (void)pm_metal_dt_unbind(d->dt_id);
            d->used = 0;
            return -1;
        }
        return d->blk_h;
    }
    return -1;
}

#if defined(PM_METAL_FIRMWARE)
static int32_t fw_blk_attach_pci(uint32_t bus, uint32_t dev, uint32_t fn) {
    volatile uint8_t *common;
    volatile uint8_t *notify;
    volatile uint8_t *devcfg;
    uint32_t notify_mult;
    uint32_t feat1;
    uint32_t i;
    uint32_t id;
    struct virtio_blk *d;
    uint8_t *desc;
    uint8_t *avail;
    uint8_t *used;
    uint16_t qsz = (uint16_t)FW_BLK_QSZ;
    if (virtio_pci_caps(bus, dev, fn, &common, &notify, &notify_mult, &devcfg) != 0) {
        return -1;
    }
    mmio_w8(common + 20, 0);
    while (mmio_r8(common + 20) != 0) {
        pm_cpu_pause();
    }
    mmio_w8(common + 20, 1u | 2u);
    mmio_w32(common + 0, 1);
    feat1 = mmio_r32(common + 4);
    mmio_w32(common + 8, 0);
    mmio_w32(common + 12, 0);
    mmio_w32(common + 8, 1);
    mmio_w32(common + 12, feat1 & 1u);
    mmio_w8(common + 20, (uint8_t)(1u | 2u | 8u));
    if ((mmio_r8(common + 20) & 8u) == 0) {
        return -1;
    }
    for (i = 0; i < VIRTIO_BLK_MAX; i++) {
        if (!s_dev[i].used) {
            break;
        }
    }
    if (i >= VIRTIO_BLK_MAX) {
        return -1;
    }
    d = &s_dev[i];
    memset(d, 0, sizeof(*d));
    d->used = 1;
    d->common = common;
    d->notify = notify;
    d->notify_mult = notify_mult;
    d->vqmem = pm_util_mem_memalign(s_arena, 4096, 4096);
    d->req = pm_util_mem_alloc(s_arena, 16);
    d->stbyte = pm_util_mem_alloc(s_arena, 1);
    if (d->vqmem == NULL || d->req == NULL || d->stbyte == NULL) {
        d->used = 0;
        return -1;
    }
    memset(d->vqmem, 0, 4096);
    mmio_w16(common + 22, 0);
    mmio_w16(common + 24, qsz);
    if (mmio_r16(common + 24) == 0) {
        d->used = 0;
        return -1;
    }
    desc = d->vqmem;
    avail = d->vqmem + 256;
    used = d->vqmem + 512;
    mmio_w64(common + 32, (uint64_t)(uintptr_t)desc);
    mmio_w64(common + 40, (uint64_t)(uintptr_t)avail);
    mmio_w64(common + 48, (uint64_t)(uintptr_t)used);
    mmio_w16(common + 28, 1);
    d->nqoff = mmio_r16(common + 30);
    d->last_used = 0;
    if (devcfg != NULL) {
        d->cap = (uint64_t)mmio_r32(devcfg) | ((uint64_t)mmio_r32(devcfg + 4) << 32);
    }
    if (d->cap == 0) {
        d->used = 0;
        return -1;
    }
    mmio_w8(common + 20, (uint8_t)(1u | 2u | 4u | 8u));
    d->ops.ready = vb_ready;
    d->ops.capacity = vb_cap;
    d->ops.read = vb_read;
    d->ops.write = vb_write;
    d->ops.close = vb_close;
    d->ops.ctx = d;
    id = pm_metal_bus_pci_cfg_read32(bus, dev, fn, 0);
    d->dt_id = pm_metal_dt_add(PM_METAL_DT_CLASS_BLK, "virtio-blk", PM_METAL_DT_BUS_PCI, bus,
        (dev << 8) | fn, id & 0xffffu, (id >> 16) & 0xffffu);
    if (d->dt_id < 0) {
        d->used = 0;
        return -1;
    }
    d->blk_h = pm_metal_drivers_blk_bind(d->dt_id, &d->ops);
    if (d->blk_h < 0) {
        d->used = 0;
        return -1;
    }
    return d->blk_h;
}
#endif

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk.virtio, pm_metal_drivers_blk_virtio_init, pm_metal_drivers_blk_virtio_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk.virtio, pm_metal_drivers_blk_virtio_deinit, pm_metal_drivers_blk_virtio_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.blk.virtio, pm_metal_drivers_blk_virtio_probe, pm_metal_drivers_blk_virtio_probe, int32_t(uint32_t));

PM_MOD_BOOT_C(pymergetic.metal.drivers.blk.virtio, pm_metal_drivers_blk_virtio_init, pm_metal_drivers_blk_virtio_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.blk.virtio, pymergetic.metal.drivers.blk);

#if defined(PM_METAL_FIRMWARE)
static int32_t virtio_blk_drv_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2,
    uint32_t loc3) {
    (void)loc2;
    (void)loc3;
    if (bus != PM_METAL_DT_BUS_PCI) {
        return -1;
    }
    return fw_blk_attach_pci(loc0, (loc1 >> 8) & 0x1fu, loc1 & 0x7u) >= 0 ? 0 : -1;
}

#include "pymergetic/metal/drivers/__types__.h"

PM_METAL_DRV_PCI_C(pymergetic.metal.drivers.blk.virtio, PM_METAL_BUS_VIRTIO_VENDOR,
    PM_METAL_BUS_VIRTIO_DEV_BLK, virtio_blk_drv_attach);
PM_METAL_DRV_PCI_C(pymergetic.metal.drivers.blk.virtio, PM_METAL_BUS_VIRTIO_VENDOR,
    PM_METAL_BUS_VIRTIO_DEV_BLK_LEGACY, virtio_blk_drv_attach);
#endif
