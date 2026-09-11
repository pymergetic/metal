/* pymergetic.metal.drivers.net.bge — instanced Broadcom Gigabit Ethernet L2.
 * mmio_up injects BAR0 (vendor word 0x14e4). PCI match is drivers_probe. */
#include "pymergetic/metal/drivers/net/bge/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/util/limits/__types__.h"
#include "pymergetic/util/mem.h"

#include <stdint.h>
#include <string.h>

#define BGE_VENDOR 0x14e4u
/* What a NIC costs is its ring, and a ring is taken when a NIC attaches: this
 * table used to stand at 8 devices x 8 frames x 2KB in every image whether or
 * not a Broadcom part was ever on the bus. The three knobs are how many NICs
 * this card offers, how deep each one's ring is, and how big a frame may be. */
#define BGE_DEVICE_DEFAULT 8u
#define BGE_QUEUE_DEFAULT 8u
#define BGE_FRAME_DEFAULT 2048u

struct bge_nic {
    uint32_t used;
    uint8_t mac[6];
    /* `qn` frames of `qframe` bytes back to back, plus one more at `scratch`:
     * poll hands a frame up while tx may write into the slot it came from. */
    uint8_t *q;
    uint8_t *scratch;
    uint16_t *ql;
    uint32_t qn;
    uint32_t qframe;
    uint32_t head;
    uint32_t n;
    uint32_t unit;
    int32_t dt_id;
    int32_t net_h;
    pm_metal_netdev_ops_t ops;
    struct bge_nic *next;
};

static pm_util_mem_arena_t *s_arena;
/* One row per NIC, taken when it attaches and kept for the seat. The netdev
 * core holds each row's address as its ops ctx, so rows are linked, never
 * moved. */
static struct bge_nic *s_head;
static uint32_t s_dev_used;

PM_UTIL_LIMIT_C(pm_metal_net_bge_limit_device, "drivers.net.bge.device",
    BGE_DEVICE_DEFAULT, 0u, &s_dev_used);
PM_UTIL_LIMIT_C(pm_metal_net_bge_limit_queue, "drivers.net.bge.queue",
    BGE_QUEUE_DEFAULT, 0u, NULL);
PM_UTIL_LIMIT_C(pm_metal_net_bge_limit_frame, "drivers.net.bge.frame",
    BGE_FRAME_DEFAULT, 0u, NULL);

/* The slot at `i` in this NIC's ring. */
static uint8_t *bge_slot(struct bge_nic *d, uint32_t i) {
    return d->q + (size_t)i * d->qframe;
}

/* This NIC's ring, at the depth and frame size the knobs say now. Kept across
 * a close/attach cycle when it already matches. */
static int32_t bge_ring_fit(struct bge_nic *d) {
    uint32_t qn = pm_metal_net_bge_limit_queue.soft;
    uint32_t qframe = pm_metal_net_bge_limit_frame.soft;
    uint8_t *q;
    uint8_t *scratch;
    uint16_t *ql;
    if (qn == 0u) {
        qn = pm_metal_net_bge_limit_queue.dflt;
    }
    if (qframe == 0u) {
        qframe = pm_metal_net_bge_limit_frame.dflt;
    }
    if (d->q != NULL && d->qn == qn && d->qframe == qframe) {
        return 0;
    }
    q = pm_util_mem_alloc(s_arena, (size_t)qn * qframe);
    scratch = pm_util_mem_alloc(s_arena, qframe);
    ql = pm_util_mem_alloc(s_arena, (size_t)qn * sizeof(*ql));
    if (q == NULL || scratch == NULL || ql == NULL) {
        return -1;
    }
    d->q = q;
    d->scratch = scratch;
    d->ql = ql;
    d->qn = qn;
    d->qframe = qframe;
    return 0;
}

static void bge_reset_q(struct bge_nic *d) {
    d->head = 0;
    d->n = 0;
}

static int32_t bge_open(void *ctx) {
    struct bge_nic *d = ctx;
    if (d == NULL) {
        return -1;
    }
    bge_reset_q(d);
    return 0;
}

static void bge_close(void *ctx) {
    struct bge_nic *d = ctx;
    if (d != NULL) {
        if (d->used != 0 && s_dev_used != 0) {
            s_dev_used--;
        }
        d->used = 0;
        d->dt_id = -1;
        d->net_h = -1;
    }
}

static void bge_mac(void *ctx, uint8_t out[6]) {
    struct bge_nic *d = ctx;
    if (d == NULL) {
        memset(out, 0, 6);
        return;
    }
    memcpy(out, d->mac, 6);
}

static int32_t bge_tx(void *ctx, const uint8_t *frame, uint16_t len) {
    struct bge_nic *d = ctx;
    uint32_t i;
    if (d == NULL || frame == NULL || len == 0 || len > d->qframe) {
        return -1;
    }
    if (d->n >= d->qn) {
        return -1;
    }
    i = (d->head + d->n) % d->qn;
    memcpy(bge_slot(d, i), frame, len);
    d->ql[i] = len;
    d->n++;
    return 0;
}

static int32_t bge_poll(void *ctx) {
    struct bge_nic *d = ctx;
    uint32_t steps;
    if (d == NULL) {
        return -1;
    }
    if (d->scratch == NULL) {
        return -1;
    }
    for (steps = 0; d->n != 0 && steps < 16u; steps++) {
        uint32_t i = d->head % d->qn;
        uint16_t n = d->ql[i];
        memcpy(d->scratch, bge_slot(d, i), n);
        d->head = (d->head + 1u) % d->qn;
        d->n--;
        (void)pm_metal_net_ip_rx_from(d->net_h, d->scratch, n);
    }
    return 0;
}

/* A row for one more NIC: a closed one first, then a fresh one. Either way
 * the device knob says whether this card may offer another NIC at all — a
 * closed row is one this card already paid for, not a free pass over the knob. NULL when this card is already offering every NIC it may. */
static struct bge_nic *bge_row(void) {
    struct bge_nic *d = s_head;
    uint32_t rows = 0;
    if (!PM_UTIL_LIMIT_ROOM(pm_metal_net_bge_limit_device, s_dev_used)) {
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

static int32_t bge_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2, uint32_t loc3) {
    struct bge_nic *d;
    int32_t dt;
    if (s_arena == NULL) {
        return -1;
    }
    dt = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "bge", bus, loc0, loc1, loc2, loc3);
    if (dt < 0) {
        return -1;
    }
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used && d->dt_id == dt) {
            return d->net_h;
        }
    }
    d = bge_row();
    if (d == NULL) {
        return -1;
    }
    if (bge_ring_fit(d) != 0) {
        return -1;
    }
    bge_reset_q(d);
    d->mac[0] = 0x02;
    d->mac[2] = 0x14;
    d->mac[3] = 0xe4;
    d->mac[5] = (uint8_t)(0x01u + d->unit);
    d->ops.open = bge_open;
    d->ops.close = bge_close;
    d->ops.mac = bge_mac;
    d->ops.tx = bge_tx;
    d->ops.poll = bge_poll;
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

int32_t pm_metal_drivers_net_bge_init(pm_util_mem_arena_t *arena) {
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

void pm_metal_drivers_net_bge_deinit(void) {
    s_head = NULL;
    s_dev_used = 0;
    s_arena = NULL;
}

int32_t pm_metal_drivers_net_bge_probe(void) {
    struct bge_nic *d;
    uint32_t unit = 0;
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used) {
            unit++;
        }
    }
    return bge_attach(PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, unit);
}

int32_t pm_metal_drivers_net_bge_mmio_up(volatile uint32_t *base) {
    uintptr_t loc;
    if (s_arena == NULL || base == NULL) {
        return -1;
    }
    if ((base[0] & 0xffffu) != BGE_VENDOR) {
        return -1;
    }
    loc = (uintptr_t)base;
    return bge_attach(PM_METAL_DT_BUS_MMIO, (uint32_t)loc, (uint32_t)((uint64_t)loc >> 32), 0, 0) >= 0
        ? 0
        : -1;
}

int32_t pm_metal_drivers_net_bge_up(void) {
    struct bge_nic *d;
    if (s_arena == NULL) {
        return -1;
    }
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used) {
            return 0;
        }
    }
    return pm_metal_drivers_net_bge_probe() >= 0 ? 0 : -1;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.bge, pm_metal_drivers_net_bge_init, pm_metal_drivers_net_bge_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.bge, pm_metal_drivers_net_bge_deinit, pm_metal_drivers_net_bge_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.bge, pm_metal_drivers_net_bge_probe, pm_metal_drivers_net_bge_probe, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.bge, pm_metal_drivers_net_bge_up, pm_metal_drivers_net_bge_up, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.bge, pm_metal_drivers_net_bge_mmio_up, pm_metal_drivers_net_bge_mmio_up, int32_t(volatile uint32_t *));

PM_MOD_BOOT_C(pymergetic.metal.drivers.net.bge, pm_metal_drivers_net_bge_init, pm_metal_drivers_net_bge_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.net.bge, pymergetic.metal.drivers.net);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.net.bge, pymergetic.metal.net.ip);

static int32_t bge_drv_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2,
    uint32_t loc3) {
    return bge_attach(bus, loc0, loc1, loc2, loc3) >= 0 ? 0 : -1;
}

#include "pymergetic/metal/drivers/__types__.h"

PM_METAL_DRV_PCI_VENDOR_C(pymergetic.metal.drivers.net.bge, BGE_VENDOR, bge_drv_attach);
