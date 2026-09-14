/* pymergetic.metal.drivers.net.virtio — instanced virtio-net (in-process vq).
 * Attach is idempotent per dt loc. PCI match is drivers_probe; mmio_up injects a bar. */
#include "pymergetic/metal/drivers/net/virtio/__exports__.h"

#include "pymergetic/metal/bus/pci.h"
#include "pymergetic/metal/bus/virtio.h"
#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/util/limits/__types__.h"
#include "pymergetic/util/mem.h"

#if defined(PM_METAL_FIRMWARE)
#include "pm_cpu.h"
#endif

#include <stdint.h>
#include <string.h>

/* virtio_net_hdr_v1: the version-1 layout always carries num_buffers, so the
 * header in front of every frame is 12 bytes, not the legacy 10. */
#define VNET_HDR 12
/* What a NIC costs is its rings, and rings are taken when a NIC attaches:
 * this table used to stand at 8 devices x 16 frames x 2KB in every image
 * whether or not a virtio-net device was ever on the bus. The knobs are how
 * many NICs this card offers, how deep the in-process ring is, and how big a
 * frame may be — the last one bounds the DMA buffers too, so raising it can
 * never let tx hand the device more than its descriptor holds. */
#define VNET_DEVICE_DEFAULT 8u
#define VNET_QUEUE_DEFAULT 8u
#define VNET_FRAME_DEFAULT 2048u

struct vnet {
    uint32_t used;
    uint8_t mac[6];
    uint16_t tx_avail;
    uint16_t tx_used;
    /* `qn` slots of VNET_HDR + `qframe` (tx) and of `qframe` (rx), taken at
     * attach. rx is handed straight up, so it needs no scratch frame. */
    uint8_t *tx_buf;
    uint16_t *tx_len;
    uint16_t rx_posted;
    uint16_t rx_filled;
    uint16_t rx_dev;
    uint16_t rx_drv;
    uint8_t *rx_buf;
    uint16_t *rx_len;
    uint32_t qn;
    uint32_t qframe;
    uint32_t unit;
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
    uint16_t avail_last;
    uint16_t tx_avail_last;
    uint8_t *vqmem;
    uint8_t *rx_data;
    uint8_t *tx_data;
#endif
    struct vnet *next;
};

static pm_util_mem_arena_t *s_arena;
/* One row per NIC, taken when it attaches and kept for the seat. The netdev
 * core holds each row's address as its ops ctx, so rows are linked, never
 * moved. */
static struct vnet *s_head;
static uint32_t s_dev_used;

PM_UTIL_LIMIT_C(pm_metal_net_virtio_limit_device, pymergetic.metal.drivers.net.virtio, device,
    VNET_DEVICE_DEFAULT, 0u, &s_dev_used);
PM_UTIL_LIMIT_C(pm_metal_net_virtio_limit_queue, pymergetic.metal.drivers.net.virtio, queue,
    VNET_QUEUE_DEFAULT, 0u, NULL);
PM_UTIL_LIMIT_C(pm_metal_net_virtio_limit_frame, pymergetic.metal.drivers.net.virtio, frame,
    VNET_FRAME_DEFAULT, 0u, NULL);

/* The tx / rx slot at `i` in this NIC's in-process ring. */
static uint8_t *vnet_tx_slot(struct vnet *d, uint32_t i) {
    return d->tx_buf + (size_t)i * (VNET_HDR + d->qframe);
}

static uint8_t *vnet_rx_slot(struct vnet *d, uint32_t i) {
    return d->rx_buf + (size_t)i * d->qframe;
}

/* This NIC's rings, at the depth and frame size the knobs say now. Kept
 * across a close/attach cycle when they already match. */
static int32_t vnet_ring_fit(struct vnet *d) {
    uint32_t qn = pm_metal_net_virtio_limit_queue.soft;
    uint32_t qframe = pm_metal_net_virtio_limit_frame.soft;
    uint8_t *tx_buf;
    uint8_t *rx_buf;
    uint16_t *tx_len;
    uint16_t *rx_len;
    if (qn == 0u) {
        qn = pm_metal_net_virtio_limit_queue.dflt;
    }
    if (qframe == 0u) {
        qframe = pm_metal_net_virtio_limit_frame.dflt;
    }
    if (d->tx_buf != NULL && d->qn == qn && d->qframe == qframe) {
        return 0;
    }
    tx_buf = pm_util_mem_alloc(s_arena, (size_t)qn * (VNET_HDR + qframe));
    rx_buf = pm_util_mem_alloc(s_arena, (size_t)qn * qframe);
    tx_len = pm_util_mem_alloc(s_arena, (size_t)qn * sizeof(*tx_len));
    rx_len = pm_util_mem_alloc(s_arena, (size_t)qn * sizeof(*rx_len));
    if (tx_buf == NULL || rx_buf == NULL || tx_len == NULL || rx_len == NULL) {
        return -1;
    }
    d->tx_buf = tx_buf;
    d->rx_buf = rx_buf;
    d->tx_len = tx_len;
    d->rx_len = rx_len;
    d->qn = qn;
    d->qframe = qframe;
    return 0;
}

#if defined(PM_METAL_FIRMWARE)
/* The frame size a NIC that has not attached yet will take. The firmware path
 * sizes its DMA buffers with this before it has a row to read it from. */
static uint32_t vnet_frame_now(void) {
    uint32_t qframe = pm_metal_net_virtio_limit_frame.soft;
    return qframe != 0u ? qframe : pm_metal_net_virtio_limit_frame.dflt;
}
#endif

#if defined(PM_METAL_FIRMWARE)
static int32_t fw_vnet_tx(struct vnet *d, const uint8_t *frame, uint16_t len);
static int32_t fw_vnet_poll(struct vnet *d);
#endif

static void vq_reset(struct vnet *d) {
    d->tx_avail = 0;
    d->tx_used = 0;
    d->rx_posted = (uint16_t)d->qn;
    d->rx_filled = 0;
    d->rx_dev = 0;
    d->rx_drv = 0;
}

static void device_run(struct vnet *d) {
    while (d->tx_used != d->tx_avail) {
        uint16_t i = (uint16_t)(d->tx_used % d->qn);
        uint16_t n = d->tx_len[i];
        d->tx_used++;
        if (n <= VNET_HDR || d->rx_posted == 0) {
            continue;
        }
        n = (uint16_t)(n - VNET_HDR);
        if (n > d->qframe) {
            n = (uint16_t)d->qframe;
        }
        memcpy(vnet_rx_slot(d, d->rx_dev % d->qn), vnet_tx_slot(d, i) + VNET_HDR, n);
        d->rx_len[d->rx_dev % d->qn] = n;
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
        if (d->used != 0 && s_dev_used != 0) {
            s_dev_used--;
        }
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

static uint32_t virtio_frame_max(void *ctx) {
    struct vnet *d = ctx;
    return d != NULL && d->qframe < PM_METAL_NET_ETH_FRAME_MAX ? d->qframe : PM_METAL_NET_ETH_FRAME_MAX;
}

static int32_t virtio_tx(void *ctx, const uint8_t *frame, uint16_t len) {
    struct vnet *d = ctx;
    uint16_t pending;
    uint16_t i;
    if (d == NULL || frame == NULL || len == 0 || len > d->qframe) {
        return -1;
    }
#if defined(PM_METAL_FIRMWARE)
    if (d->common != NULL) {
        return fw_vnet_tx(d, frame, len);
    }
#endif
    pending = (uint16_t)(d->tx_avail - d->tx_used);
    if (pending >= d->qn) {
        return PM_METAL_NET_TX_WAIT;
    }
    i = (uint16_t)(d->tx_avail % d->qn);
    memset(vnet_tx_slot(d, i), 0, VNET_HDR);
    memcpy(vnet_tx_slot(d, i) + VNET_HDR, frame, len);
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
        uint16_t i = (uint16_t)(d->rx_drv % d->qn);
        uint16_t n = d->rx_len[i];
        d->rx_drv++;
        d->rx_filled--;
        d->rx_posted++;
        (void)pm_metal_net_ip_rx_from(d->net_h, vnet_rx_slot(d, i), n);
    }
    return 0;
}

/* A row for one more NIC: a closed one first, then a fresh one. Either way
 * the device knob says whether this card may offer another NIC at all — a
 * closed row is one this card already paid for, not a free pass over the knob. NULL when this card is already offering every NIC it may. */
static struct vnet *vnet_row(void) {
    struct vnet *d = s_head;
    uint32_t rows = 0;
    if (!PM_UTIL_LIMIT_ROOM(pm_metal_net_virtio_limit_device, s_dev_used)) {
        return NULL;
    }
    while (d != NULL) {
        if (!d->used) {
            return d;
        }
        rows++;
        d = d->next;
    }
    d = pm_util_mem_alloc(s_arena, sizeof(*d));
    if (d == NULL) {
        return NULL;
    }
    memset(d, 0, sizeof(*d));
    d->unit = rows;
    d->dt_id = -1;
    d->net_h = -1;
    d->next = s_head;
    s_head = d;
    return d;
}

static int32_t vnet_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2, uint32_t loc3) {
    struct vnet *d;
    int32_t dt;
    if (s_arena == NULL) {
        return -1;
    }
    dt = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "virtio-net", bus, loc0, loc1, loc2, loc3);
    if (dt < 0) {
        return -1;
    }
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used && d->dt_id == dt) {
            return d->net_h;
        }
    }
    d = vnet_row();
    if (d == NULL) {
        return -1;
    }
    if (vnet_ring_fit(d) != 0) {
        return -1;
    }
    vq_reset(d);
    d->mac[0] = 0x02;
    d->mac[5] = (uint8_t)(0x04u + d->unit);
    d->ops.open = virtio_open;
    d->ops.close = virtio_close;
    d->ops.mac = virtio_mac;
    d->ops.frame_max = virtio_frame_max;
    d->ops.tx = virtio_tx;
    d->ops.poll = virtio_poll;
    d->ops.ctx = d;
    d->dt_id = dt;
    d->used = 1;
    d->net_h = pm_metal_drivers_net_bind(dt, &d->ops);
    if (d->net_h < 0) {
        d->used = 0;
        return -1;
    }
    s_dev_used++;
    return d->net_h;
}

int32_t pm_metal_drivers_net_virtio_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    /* Rows came from the arena the last run was given; this one may be a
     * different arena, so the chain starts empty. */
    s_head = NULL;
    s_dev_used = 0;
    return 0;
}

void pm_metal_drivers_net_virtio_deinit(void) {
    s_head = NULL;
    s_dev_used = 0;
    s_arena = NULL;
}

int32_t pm_metal_drivers_net_virtio_probe(void) {
    struct vnet *d;
    uint32_t unit = 0;
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used) {
            unit++;
        }
    }
    return vnet_attach(PM_METAL_DT_BUS_VIRTIO, 0, 0, 0, unit);
}

int32_t pm_metal_drivers_net_virtio_up(void) {
    struct vnet *d;
    if (s_arena == NULL) {
        return -1;
    }
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used) {
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
#define FW_TX_SPINS 200000u
#define FW_DESC_F_NEXT 1u
#define FW_DESC_F_WRITE 2u
#define VIRTIO_PCI_CAP_COMMON 1u
#define VIRTIO_PCI_CAP_NOTIFY 2u
#define VIRTIO_PCI_CAP_DEVCFG 4u
#define VIRTIO_NET_F_MAC 5u
#define VIRTIO_F_VERSION_1 32u

/* Ring memory is plain RAM the device also reads and writes, so indices are
 * loaded and stored a byte at a time in little endian and never cached. */
static uint16_t ring_ld16(volatile const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void ring_st16(volatile uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void ring_st32(volatile uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

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
    /* One buffer per descriptor. Both rings are the same size, so the data area
     * the caller passed is qsz buffers wide on the receive and the send side. */
    memset(data, 0, (size_t)qsz * (VNET_HDR + d->qframe));
    for (i = 0; i < qsz; i++) {
        uint64_t addr = (uint64_t)(uintptr_t)(data + i * (VNET_HDR + d->qframe));
        uint8_t *de = desc + i * 16u;
        ring_st32(de, (uint32_t)addr);
        ring_st32(de + 4, (uint32_t)(addr >> 32));
        if (rx) {
            ring_st32(de + 8, (uint32_t)(VNET_HDR + d->qframe));
            ring_st16(de + 12, (uint16_t)FW_DESC_F_WRITE);
            ring_st16(avail + 4 + i * 2u, (uint16_t)i);
        }
    }
    if (rx) {
        ring_st16(avail + 2, qsz);
    }
    mmio_w64(c + 32, (uint64_t)(uintptr_t)desc);
    mmio_w64(c + 40, (uint64_t)(uintptr_t)avail);
    mmio_w64(c + 48, (uint64_t)(uintptr_t)used);
    mmio_w16(c + 28, 1);
    if (rx) {
        d->rx_nqoff = mmio_r16(c + 30);
        d->rx_last = 0;
        d->avail_last = qsz; /* mirrors the avail idx we just published */
        fw_notify(d, 0, d->rx_nqoff);
    } else {
        d->tx_nqoff = mmio_r16(c + 30);
        d->tx_last = 0;
        d->tx_avail_last = 0;
    }
    (void)used;
    return 0;
}

/* Take back the send buffers the device is finished with. Until it hands a
 * descriptor back, the frame in that buffer is still being read. */
static void fw_vnet_tx_reap(struct vnet *d) {
    volatile uint8_t *used = d->vqmem + 4096 + 512;
    d->tx_last = ring_ld16(used + 2);
}

static int32_t fw_vnet_tx(struct vnet *d, const uint8_t *frame, uint16_t len) {
    uint8_t *desc = d->vqmem + 4096;
    uint8_t *avail = desc + 256;
    uint8_t *buf;
    uint16_t aidx;
    uint16_t slot;
    uint32_t spins;
    if (d->tx_data == NULL || len == 0 || len > d->qframe) {
        return -1;
    }
    /* Claim the avail tail before touching the ring — tx is exported on the
     * same face any core may call (the board proves tx unlocked next to the
     * runners' locked pumps, same race the rx claim fixes). */
    for (spins = 0; ; spins++) {
        aidx = d->tx_avail_last;
        fw_vnet_tx_reap(d);
        if ((uint16_t)(aidx - d->tx_last) < (uint16_t)FW_QSZ) {
            if (__atomic_compare_exchange_n(&d->tx_avail_last, &aidx, (uint16_t)(aidx + 1u), 1,
                    __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
                break;
            }
            spins = 0;
            continue;
        }
        if (spins >= FW_TX_SPINS) {
            return PM_METAL_NET_TX_WAIT;
        }
        pm_cpu_pause();
    }
    slot = (uint16_t)(aidx % FW_QSZ);
    buf = d->tx_data + (uint32_t)slot * (VNET_HDR + d->qframe);
    memset(buf, 0, VNET_HDR);
    memcpy(buf + VNET_HDR, frame, len);
    ring_st32(desc + (uint32_t)slot * 16u + 8u, (uint32_t)(VNET_HDR + len));
    ring_st16(desc + (uint32_t)slot * 16u + 12u, 0);
    ring_st16(avail + 4 + (uint32_t)slot * 2u, slot);
    /* The index publishes the entry; the entry has to be there first. */
    pm_cpu_store_fence();
    ring_st16(avail + 2, (uint16_t)(aidx + 1u));
    pm_cpu_store_fence();
    fw_notify(d, 1, d->tx_nqoff);
    return 0;
}

static int32_t fw_vnet_poll(struct vnet *d) {
    uint8_t *used = d->vqmem + 512;
    uint8_t *avail = d->vqmem + 256;
    uint16_t uidx;
    if (d->rx_data == NULL) {
        return -1;
    }
    uidx = ring_ld16(used + 2);
    /* Claim-based consumption. The poll face is callable from any core: the
     * async runners pump under the net.ip lock, but board proves poll a
     * device directly, and on smp seats those overlap. The old
     * `while (rx_last != uidx) rx_last++` equality exit turned one lost
     * race into a catastrophe — a second incrementer overshooting the
     * device's idx leaves rx_last "ahead", the equality never re-matches
     * until the counter wraps, so a single poll spun up to 65536
     * iterations re-queueing slot 0 and re-delivering stale frames (a
     * boot rx storm of ~64k frames on one nic; every delivery acked). A
     * claim re-checks the fresh gap before each increment, so two cores
     * split the backlog and neither can pass the device's frontier. */
    for (;;) {
        uint16_t last = d->rx_last;
        uint32_t slot;
        uint8_t *ue;
        uint16_t id;
        uint16_t n;
        uint8_t *pkt;
        uint16_t at;
        if ((uint16_t)(uidx - last) == 0u) {
            break;
        }
        if (!__atomic_compare_exchange_n(&d->rx_last, &last, (uint16_t)(last + 1u), 1,
                __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            continue; /* the other core claimed it; re-read the gap */
        }
        slot = (uint32_t)(last % FW_QSZ);
        ue = used + 4 + slot * 8u;
        id = (uint16_t)ring_ld16(ue);
        n = (uint16_t)ring_ld16(ue + 4);
        pkt = d->rx_data + (uint32_t)id * (VNET_HDR + d->qframe);
        if (n > VNET_HDR) {
            n = (uint16_t)(n - VNET_HDR);
            if (n > d->qframe) {
                n = (uint16_t)d->qframe;
            }
            (void)pm_metal_net_ip_rx_from(d->net_h, pkt + VNET_HDR, n);
        }
        /* Hand the buffer back: claim the avail tail the same way, so two
         * pollers never write the same avail slot or double-bump its idx. */
        at = d->avail_last;
        for (;;) {
            if (__atomic_compare_exchange_n(&d->avail_last, &at, (uint16_t)(at + 1u), 1,
                    __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
                break;
            }
        }
        ring_st16(avail + 4 + (uint32_t)(at % FW_QSZ) * 2u, id);
        pm_cpu_store_fence();
        ring_st16(avail + 2, (uint16_t)(at + 1u));
        uidx = ring_ld16(used + 2);
    }
    fw_notify(d, 0, d->rx_nqoff);
    return 0;
}

static int32_t fw_vnet_attach_pci(uint32_t bus, uint32_t dev, uint32_t fn) {
    volatile uint8_t *common;
    volatile uint8_t *notify;
    volatile uint8_t *devcfg;
    uint32_t notify_mult;
    uint32_t id;
    struct vnet *d;
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
    d = vnet_row();
    if (d == NULL) {
        return -1;
    }
    /* The in-process ring this row would use is not taken on a real device:
     * the frames go through the vring below. The frame size is the same knob,
     * so a seat that raises it raises what the descriptors hold too. */
    d->qframe = vnet_frame_now();
    d->qn = FW_QSZ;
    d->used = 1;
    d->common = common;
    d->notify = notify;
    d->notify_mult = notify_mult;
    d->qsz = (uint16_t)FW_QSZ;
    d->vqmem = pm_util_mem_memalign(s_arena, 4096, 8192);
    d->rx_data = pm_util_mem_alloc(s_arena, (size_t)FW_QSZ * (VNET_HDR + d->qframe));
    d->tx_data = pm_util_mem_alloc(s_arena, (size_t)FW_QSZ * (VNET_HDR + d->qframe));
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
        d->mac[5] = (uint8_t)(0x10u + d->unit);
    }
    mmio_w8(common + 20, (uint8_t)(1u | 2u | 4u | 8u));
    d->ops.open = virtio_open;
    d->ops.close = virtio_close;
    d->ops.mac = virtio_mac;
    d->ops.frame_max = virtio_frame_max;
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
    s_dev_used++;
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
