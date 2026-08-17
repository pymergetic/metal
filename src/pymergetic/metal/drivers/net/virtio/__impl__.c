/* pymergetic.metal.drivers.net.virtio — instanced virtio-net (in-process vq).
 * Attach is idempotent per dt loc. PCI match is drivers_probe; mmio_up injects a bar. */
#include "pymergetic/metal/drivers/net/virtio/__exports__.h"

#include "pymergetic/metal/bus/pci.h"
#include "pymergetic/metal/bus/virtio.h"
#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/util/mem.h"

#if defined(PM_METAL_FIRMWARE)
#include "pm_cpu.h"
#endif

#include <stdint.h>
#include <string.h>

#define VQ_N 8
#define VNET_HDR 10
#define FRAME_MAX 2048
#define VNET_MAX 8u

struct vnet {
    uint32_t used;
    uint8_t mac[6];
    uint16_t tx_avail;
    uint16_t tx_used;
    uint8_t tx_buf[VQ_N][VNET_HDR + FRAME_MAX];
    uint16_t tx_len[VQ_N];
    uint16_t rx_posted;
    uint16_t rx_filled;
    uint16_t rx_dev;
    uint16_t rx_drv;
    uint8_t rx_buf[VQ_N][FRAME_MAX];
    uint16_t rx_len[VQ_N];
    int32_t dt_id;
    int32_t net_h;
    pm_metal_netdev_ops_t ops;
#if defined(PM_METAL_FIRMWARE)
    volatile uint8_t *common;
    volatile uint8_t *notify;
    uint32_t notify_mult;
    uint16_t qsz;
    uint16_t rx_nqoff;
    uint16_t tx_nqoff;
    uint16_t rx_last;
    uint16_t tx_last;
    uint8_t *vqmem;
    uint8_t *rx_data;
    uint8_t *tx_data;
#endif
};

static pm_util_mem_arena_t *s_arena;
static struct vnet s_dev[VNET_MAX];

#if defined(PM_METAL_FIRMWARE)
static int32_t fw_vnet_tx(struct vnet *d, const uint8_t *frame, uint16_t len);
static int32_t fw_vnet_poll(struct vnet *d);
#endif

static void vq_reset(struct vnet *d) {
    d->tx_avail = 0;
    d->tx_used = 0;
    d->rx_posted = VQ_N;
    d->rx_filled = 0;
    d->rx_dev = 0;
    d->rx_drv = 0;
}

static void device_run(struct vnet *d) {
    while (d->tx_used != d->tx_avail) {
        uint16_t i = (uint16_t)(d->tx_used % VQ_N);
        uint16_t n = d->tx_len[i];
        d->tx_used++;
        if (n <= VNET_HDR || d->rx_posted == 0) {
            continue;
        }
        n = (uint16_t)(n - VNET_HDR);
        if (n > FRAME_MAX) {
            n = FRAME_MAX;
        }
        memcpy(d->rx_buf[d->rx_dev % VQ_N], d->tx_buf[i] + VNET_HDR, n);
        d->rx_len[d->rx_dev % VQ_N] = n;
        d->rx_dev++;
        d->rx_posted--;
        d->rx_filled++;
    }
}

static int32_t virtio_open(void *ctx) {
    struct vnet *d = ctx;
    if (d == NULL) {
        return -1;
    }
    vq_reset(d);
    return 0;
}

static void virtio_close(void *ctx) {
    struct vnet *d = ctx;
    if (d != NULL) {
        d->used = 0;
        d->dt_id = -1;
        d->net_h = -1;
    }
}

static void virtio_mac(void *ctx, uint8_t out[6]) {
    struct vnet *d = ctx;
    if (d == NULL) {
        memset(out, 0, 6);
        return;
    }
    memcpy(out, d->mac, 6);
}

static int32_t virtio_tx(void *ctx, const uint8_t *frame, uint16_t len) {
    struct vnet *d = ctx;
    uint16_t pending;
    uint16_t i;
    if (d == NULL || frame == NULL || len == 0 || len > FRAME_MAX) {
        return -1;
    }
#if defined(PM_METAL_FIRMWARE)
    if (d->common != NULL) {
        return fw_vnet_tx(d, frame, len);
    }
#endif
    pending = (uint16_t)(d->tx_avail - d->tx_used);
    if (pending >= VQ_N) {
        return -1;
    }
    i = (uint16_t)(d->tx_avail % VQ_N);
    memset(d->tx_buf[i], 0, VNET_HDR);
    memcpy(d->tx_buf[i] + VNET_HDR, frame, len);
    d->tx_len[i] = (uint16_t)(VNET_HDR + len);
    d->tx_avail++;
    device_run(d);
    return 0;
}

static int32_t virtio_poll(void *ctx) {
    struct vnet *d = ctx;
    if (d == NULL) {
        return -1;
    }
#if defined(PM_METAL_FIRMWARE)
    if (d->common != NULL) {
        return fw_vnet_poll(d);
    }
#endif
    device_run(d);
    while (d->rx_filled != 0) {
        uint16_t i = (uint16_t)(d->rx_drv % VQ_N);
        uint16_t n = d->rx_len[i];
        d->rx_drv++;
        d->rx_filled--;
        d->rx_posted++;
        (void)pm_metal_net_ip_rx_from(d->net_h, d->rx_buf[i], n);
    }
    return 0;
}

static int32_t vnet_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2, uint32_t loc3) {
    uint32_t i;
    struct vnet *d;
    int32_t dt;
    if (s_arena == NULL) {
        return -1;
    }
    dt = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "virtio-net", bus, loc0, loc1, loc2, loc3);
    if (dt < 0) {
        return -1;
    }
    for (i = 0; i < VNET_MAX; i++) {
        if (s_dev[i].used && s_dev[i].dt_id == dt) {
            return s_dev[i].net_h;
        }
    }
    for (i = 0; i < VNET_MAX; i++) {
        if (s_dev[i].used) {
            continue;
        }
        d = &s_dev[i];
        memset(d, 0, sizeof(*d));
        d->used = 1;
        d->mac[0] = 0x02;
        d->mac[5] = (uint8_t)(0x04u + i);
        d->ops.open = virtio_open;
        d->ops.close = virtio_close;
        d->ops.mac = virtio_mac;
        d->ops.tx = virtio_tx;
        d->ops.poll = virtio_poll;
        d->ops.ctx = d;
        d->dt_id = dt;
        d->net_h = pm_metal_drivers_net_bind(dt, &d->ops);
        if (d->net_h < 0) {
            d->used = 0;
            return -1;
        }
        return d->net_h;
    }
    return -1;
}

int32_t pm_metal_drivers_net_virtio_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_dev, 0, sizeof(s_dev));
    return 0;
}

void pm_metal_drivers_net_virtio_deinit(void) {
    memset(s_dev, 0, sizeof(s_dev));
    s_arena = NULL;
}

int32_t pm_metal_drivers_net_virtio_probe(void) {
    uint32_t i;
    for (i = 0; i < VNET_MAX; i++) {
        if (!s_dev[i].used) {
            return vnet_attach(PM_METAL_DT_BUS_VIRTIO, 0, 0, 0, i);
        }
    }
    return -1;
}

int32_t pm_metal_drivers_net_virtio_up(void) {
    uint32_t i;
    if (s_arena == NULL) {
        return -1;
    }
    for (i = 0; i < VNET_MAX; i++) {
        if (s_dev[i].used) {
            return 0;
        }
    }
    return vnet_attach(PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, 0) >= 0 ? 0 : -1;
}

int32_t pm_metal_drivers_net_virtio_mmio_up(volatile uint32_t *base) {
    uint32_t st;
    uintptr_t loc;
    if (s_arena == NULL || base == NULL) {
        return -1;
    }
    if (base[0x000 / 4] != 0x74726976u) {
        return -1;
    }
    if (base[0x008 / 4] != 1u) {
        return -1;
    }
    st = 1u | 2u;
    base[0x070 / 4] = st;
    st |= 8u;
    base[0x070 / 4] = st;
    if ((base[0x070 / 4] & 8u) == 0) {
        return -1;
    }
    st |= 4u;
    base[0x070 / 4] = st;
    loc = (uintptr_t)base;
    return vnet_attach(PM_METAL_DT_BUS_MMIO, (uint32_t)loc, (uint32_t)((uint64_t)loc >> 32), 0, 0) >= 0
        ? 0
        : -1;
}

#if defined(PM_METAL_FIRMWARE)
#define FW_QSZ 16u
#define FW_DESC_F_NEXT 1u
#define FW_DESC_F_WRITE 2u
#define VIRTIO_PCI_CAP_COMMON 1u
#define VIRTIO_PCI_CAP_NOTIFY 2u
#define VIRTIO_PCI_CAP_DEVCFG 4u
#define VIRTIO_NET_F_MAC 5u
#define VIRTIO_F_VERSION_1 32u

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

static void fw_notify(struct vnet *d, uint16_t qidx, uint16_t nqoff) {
    volatile uint8_t *p = d->notify + (uint32_t)nqoff * d->notify_mult;
    mmio_w16(p, qidx);
}

static int32_t fw_setup_queue(struct vnet *d, uint16_t qidx, uint8_t *mem, uint8_t *data, int rx) {
    uint32_t i;
    uint16_t qsz = (uint16_t)FW_QSZ;
    volatile uint8_t *c = d->common;
    uint8_t *desc = mem;
    uint8_t *avail = mem + 256;
    uint8_t *used = mem + 512;
    mmio_w16(c + 22, qidx);
    mmio_w16(c + 24, qsz);
    if (mmio_r16(c + 24) == 0) {
        return -1;
    }
    memset(mem, 0, 4096);
    memset(data, 0, (size_t)qsz * (VNET_HDR + FRAME_MAX));
    for (i = 0; i < qsz; i++) {
        uint64_t addr = (uint64_t)(uintptr_t)(data + i * (VNET_HDR + FRAME_MAX));
        uint8_t *de = desc + i * 16u;
        de[0] = (uint8_t)addr;
        de[1] = (uint8_t)(addr >> 8);
        de[2] = (uint8_t)(addr >> 16);
        de[3] = (uint8_t)(addr >> 24);
        de[4] = (uint8_t)(addr >> 32);
        de[5] = (uint8_t)(addr >> 40);
        de[6] = (uint8_t)(addr >> 48);
        de[7] = (uint8_t)(addr >> 56);
        de[8] = (uint8_t)(VNET_HDR + FRAME_MAX);
        de[9] = (uint8_t)((VNET_HDR + FRAME_MAX) >> 8);
        if (rx) {
            de[12] = (uint8_t)FW_DESC_F_WRITE;
            avail[4 + i * 2u] = (uint8_t)i;
            avail[5 + i * 2u] = 0;
        }
    }
    if (rx) {
        avail[2] = (uint8_t)qsz;
        avail[3] = 0;
    }
    mmio_w64(c + 32, (uint64_t)(uintptr_t)desc);
    mmio_w64(c + 40, (uint64_t)(uintptr_t)avail);
    mmio_w64(c + 48, (uint64_t)(uintptr_t)used);
    mmio_w16(c + 28, 1);
    if (rx) {
        d->rx_nqoff = mmio_r16(c + 30);
        d->rx_last = 0;
        fw_notify(d, 0, d->rx_nqoff);
    } else {
        d->tx_nqoff = mmio_r16(c + 30);
        d->tx_last = 0;
    }
    (void)used;
    return 0;
}

static int32_t fw_vnet_tx(struct vnet *d, const uint8_t *frame, uint16_t len) {
    uint8_t *desc = d->vqmem + 4096;
    uint8_t *avail = desc + 256;
    uint16_t idx;
    uint16_t aidx;
    if (d->tx_data == NULL || len > FRAME_MAX) {
        return -1;
    }
    memset(d->tx_data, 0, VNET_HDR);
    memcpy(d->tx_data + VNET_HDR, frame, len);
    desc[8] = (uint8_t)(VNET_HDR + len);
    desc[9] = (uint8_t)((VNET_HDR + len) >> 8);
    desc[12] = 0;
    desc[13] = 0;
    aidx = (uint16_t)(avail[2] | ((uint16_t)avail[3] << 8));
    idx = (uint16_t)(aidx % FW_QSZ);
    avail[4 + idx * 2u] = 0;
    avail[5 + idx * 2u] = 0;
    aidx++;
    avail[2] = (uint8_t)aidx;
    avail[3] = (uint8_t)(aidx >> 8);
    pm_cpu_store_fence();
    fw_notify(d, 1, d->tx_nqoff);
    return 0;
}

static int32_t fw_vnet_poll(struct vnet *d) {
    uint8_t *used = d->vqmem + 512;
    uint8_t *avail = d->vqmem + 256;
    uint16_t uidx;
    uint16_t aidx;
    if (d->rx_data == NULL) {
        return -1;
    }
    uidx = (uint16_t)(used[2] | ((uint16_t)used[3] << 8));
    while (d->rx_last != uidx) {
        uint32_t slot = (uint32_t)(d->rx_last % FW_QSZ);
        uint8_t *ue = used + 4 + slot * 8u;
        uint16_t id = (uint16_t)(ue[0] | ((uint16_t)ue[1] << 8));
        uint16_t n = (uint16_t)(ue[4] | ((uint16_t)ue[5] << 8));
        uint8_t *pkt = d->rx_data + (uint32_t)id * (VNET_HDR + FRAME_MAX);
        if (n > VNET_HDR) {
            n = (uint16_t)(n - VNET_HDR);
            if (n > FRAME_MAX) {
                n = FRAME_MAX;
            }
            (void)pm_metal_net_ip_rx_from(d->net_h, pkt + VNET_HDR, n);
        }
        aidx = (uint16_t)(avail[2] | ((uint16_t)avail[3] << 8));
        avail[4 + (aidx % FW_QSZ) * 2u] = (uint8_t)id;
        avail[5 + (aidx % FW_QSZ) * 2u] = 0;
        aidx++;
        avail[2] = (uint8_t)aidx;
        avail[3] = (uint8_t)(aidx >> 8);
        d->rx_last++;
        uidx = (uint16_t)(used[2] | ((uint16_t)used[3] << 8));
    }
    return 0;
}

static int32_t fw_vnet_attach_pci(uint32_t bus, uint32_t dev, uint32_t fn) {
    volatile uint8_t *common;
    volatile uint8_t *notify;
    volatile uint8_t *devcfg;
    uint32_t notify_mult;
    uint32_t id;
    struct vnet *d;
    uint32_t i;
    uint32_t feat0;
    uint32_t feat1;
    if (virtio_pci_caps(bus, dev, fn, &common, &notify, &notify_mult, &devcfg) != 0) {
        return -1;
    }
    mmio_w8(common + 20, 0);
    while (mmio_r8(common + 20) != 0) {
        pm_cpu_pause();
    }
    mmio_w8(common + 20, 1u | 2u);
    mmio_w32(common + 0, 0);
    feat0 = mmio_r32(common + 4);
    mmio_w32(common + 0, 1);
    feat1 = mmio_r32(common + 4);
    (void)feat0;
    mmio_w32(common + 8, 0);
    mmio_w32(common + 12, (1u << VIRTIO_NET_F_MAC));
    mmio_w32(common + 8, 1);
    mmio_w32(common + 12, (feat1 & 1u));
    mmio_w8(common + 20, (uint8_t)(1u | 2u | 8u));
    if ((mmio_r8(common + 20) & 8u) == 0) {
        return -1;
    }
    for (i = 0; i < VNET_MAX; i++) {
        if (!s_dev[i].used) {
            break;
        }
    }
    if (i >= VNET_MAX) {
        return -1;
    }
    d = &s_dev[i];
    memset(d, 0, sizeof(*d));
    d->used = 1;
    d->common = common;
    d->notify = notify;
    d->notify_mult = notify_mult;
    d->qsz = (uint16_t)FW_QSZ;
    d->vqmem = pm_util_mem_memalign(s_arena, 4096, 8192);
    d->rx_data = pm_util_mem_alloc(s_arena, (size_t)FW_QSZ * (VNET_HDR + FRAME_MAX));
    d->tx_data = pm_util_mem_alloc(s_arena, VNET_HDR + FRAME_MAX);
    if (d->vqmem == NULL || d->rx_data == NULL || d->tx_data == NULL) {
        d->used = 0;
        return -1;
    }
    if (fw_setup_queue(d, 0, d->vqmem, d->rx_data, 1) != 0
        || fw_setup_queue(d, 1, d->vqmem + 4096, d->tx_data, 0) != 0) {
        d->used = 0;
        return -1;
    }
    if (devcfg != NULL) {
        memcpy(d->mac, (const void *)devcfg, 6);
    } else {
        d->mac[0] = 0x02;
        d->mac[5] = (uint8_t)(0x10u + i);
    }
    mmio_w8(common + 20, (uint8_t)(1u | 2u | 4u | 8u));
    d->ops.open = virtio_open;
    d->ops.close = virtio_close;
    d->ops.mac = virtio_mac;
    d->ops.tx = virtio_tx;
    d->ops.poll = virtio_poll;
    d->ops.ctx = d;
    id = pm_metal_bus_pci_cfg_read32(bus, dev, fn, 0);
    d->dt_id = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "virtio-net", PM_METAL_DT_BUS_PCI, bus,
        (dev << 8) | fn, id & 0xffffu, (id >> 16) & 0xffffu);
    if (d->dt_id < 0) {
        d->used = 0;
        return -1;
    }
    d->net_h = pm_metal_drivers_net_bind(d->dt_id, &d->ops);
    if (d->net_h < 0) {
        d->used = 0;
        return -1;
    }
    return d->net_h;
}
#endif

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.virtio, pm_metal_drivers_net_virtio_init, pm_metal_drivers_net_virtio_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.virtio, pm_metal_drivers_net_virtio_deinit, pm_metal_drivers_net_virtio_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.virtio, pm_metal_drivers_net_virtio_probe, pm_metal_drivers_net_virtio_probe, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.virtio, pm_metal_drivers_net_virtio_up, pm_metal_drivers_net_virtio_up, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.virtio, pm_metal_drivers_net_virtio_mmio_up, pm_metal_drivers_net_virtio_mmio_up, int32_t(volatile uint32_t *));

PM_MOD_BOOT_C(pymergetic.metal.drivers.net.virtio, pm_metal_drivers_net_virtio_init, pm_metal_drivers_net_virtio_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.net.virtio, pymergetic.metal.drivers.net);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.net.virtio, pymergetic.metal.bus.virtio);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.net.virtio, pymergetic.metal.net.ip);

static int32_t virtio_net_drv_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2,
    uint32_t loc3) {
#if defined(PM_METAL_FIRMWARE)
    if (bus == PM_METAL_DT_BUS_PCI) {
        return fw_vnet_attach_pci(loc0, (loc1 >> 8) & 0x1fu, loc1 & 0x7u) >= 0 ? 0 : -1;
    }
#endif
    return vnet_attach(bus, loc0, loc1, loc2, loc3) >= 0 ? 0 : -1;
}

#include "pymergetic/metal/drivers/__types__.h"

PM_METAL_DRV_PCI_C(pymergetic.metal.drivers.net.virtio, PM_METAL_BUS_VIRTIO_VENDOR,
    PM_METAL_BUS_VIRTIO_DEV_NET, virtio_net_drv_attach);
PM_METAL_DRV_PCI_C(pymergetic.metal.drivers.net.virtio, PM_METAL_BUS_VIRTIO_VENDOR,
    PM_METAL_BUS_VIRTIO_DEV_NET_LEGACY, virtio_net_drv_attach);
