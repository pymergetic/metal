/* pymergetic.metal.drivers.net.bge — instanced Broadcom Gigabit Ethernet L2.
 * mmio_up injects BAR0 (vendor word 0x14e4). PCI match is drivers_probe. */
#include "pymergetic/metal/drivers/net/bge/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/net/ip.h"

#include <stdint.h>
#include <string.h>

#define BGE_Q 8
#define BGE_FRAME 2048
#define BGE_VENDOR 0x14e4u
#define BGE_MAX 8u

struct bge_nic {
    uint32_t used;
    uint8_t mac[6];
    uint8_t q[BGE_Q][BGE_FRAME];
    uint16_t ql[BGE_Q];
    uint32_t head;
    uint32_t n;
    int32_t dt_id;
    int32_t net_h;
    pm_metal_netdev_ops_t ops;
};

static pm_util_mem_arena_t *s_arena;
static struct bge_nic s_dev[BGE_MAX];

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
    if (d == NULL || frame == NULL || len == 0 || len > BGE_FRAME) {
        return -1;
    }
    if (d->n >= BGE_Q) {
        return -1;
    }
    i = (d->head + d->n) % BGE_Q;
    memcpy(d->q[i], frame, len);
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
    for (steps = 0; d->n != 0 && steps < 16u; steps++) {
        uint32_t i = d->head % BGE_Q;
        uint16_t n = d->ql[i];
        uint8_t frame[BGE_FRAME];
        memcpy(frame, d->q[i], n);
        d->head = (d->head + 1u) % BGE_Q;
        d->n--;
        (void)pm_metal_net_ip_rx_from(d->net_h, frame, n);
    }
    return 0;
}

static int32_t bge_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2, uint32_t loc3) {
    uint32_t i;
    struct bge_nic *d;
    int32_t dt;
    if (s_arena == NULL) {
        return -1;
    }
    dt = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "bge", bus, loc0, loc1, loc2, loc3);
    if (dt < 0) {
        return -1;
    }
    for (i = 0; i < BGE_MAX; i++) {
        if (s_dev[i].used && s_dev[i].dt_id == dt) {
            return s_dev[i].net_h;
        }
    }
    for (i = 0; i < BGE_MAX; i++) {
        if (s_dev[i].used) {
            continue;
        }
        d = &s_dev[i];
        memset(d, 0, sizeof(*d));
        d->used = 1;
        d->mac[0] = 0x02;
        d->mac[2] = 0x14;
        d->mac[3] = 0xe4;
        d->mac[5] = (uint8_t)(0x01u + i);
        d->ops.open = bge_open;
        d->ops.close = bge_close;
        d->ops.mac = bge_mac;
        d->ops.tx = bge_tx;
        d->ops.poll = bge_poll;
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

int32_t pm_metal_drivers_net_bge_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_dev, 0, sizeof(s_dev));
    return 0;
}

void pm_metal_drivers_net_bge_deinit(void) {
    memset(s_dev, 0, sizeof(s_dev));
    s_arena = NULL;
}

int32_t pm_metal_drivers_net_bge_probe(void) {
    uint32_t i;
    for (i = 0; i < BGE_MAX; i++) {
        if (!s_dev[i].used) {
            return bge_attach(PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, i);
        }
    }
    return -1;
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
    uint32_t i;
    if (s_arena == NULL) {
        return -1;
    }
    for (i = 0; i < BGE_MAX; i++) {
        if (s_dev[i].used) {
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
