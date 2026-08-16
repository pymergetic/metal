/* pymergetic.metal.drivers.net.sim — instanced in-process L2.
 * Browser: JS import in this card's library.js (metal.mk --js-library). */
#include "pymergetic/metal/drivers/net/sim/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/net/ip.h"

#include <string.h>

#if defined(__EMSCRIPTEN__)
extern int32_t pm_metal_drivers_net_sim_js_tx(const uint8_t *frame, uint16_t len);
extern int32_t pm_metal_drivers_net_sim_js_rx(uint8_t *frame, uint16_t max);
#endif

#define SIM_Q 8
#define SIM_FRAME 2048
#define SIM_MAX 4u

struct sim_nic {
    uint32_t used;
    uint32_t drop;
    uint8_t mac[6];
#if !defined(__EMSCRIPTEN__)
    uint8_t q[SIM_Q][SIM_FRAME];
    uint16_t ql[SIM_Q];
    uint32_t head;
    uint32_t n;
#endif
    int32_t dt_id;
    int32_t net_h;
    pm_metal_netdev_ops_t ops;
};

static pm_util_mem_arena_t *s_arena;
static struct sim_nic s_dev[SIM_MAX];

static void sim_reset_q(struct sim_nic *d) {
#if !defined(__EMSCRIPTEN__)
    d->head = 0;
    d->n = 0;
#endif
    d->drop = 0;
}

static int32_t sim_open(void *ctx) {
    struct sim_nic *d = ctx;
    if (d == NULL) {
        return -1;
    }
    sim_reset_q(d);
    return 0;
}

static void sim_close(void *ctx) {
    struct sim_nic *d = ctx;
    if (d != NULL) {
        d->used = 0;
        d->dt_id = -1;
        d->net_h = -1;
    }
}

static void sim_mac(void *ctx, uint8_t out[6]) {
    struct sim_nic *d = ctx;
    if (d == NULL) {
        memset(out, 0, 6);
        return;
    }
    memcpy(out, d->mac, 6);
}

static int32_t sim_tx(void *ctx, const uint8_t *frame, uint16_t len) {
    struct sim_nic *d = ctx;
    if (d == NULL || frame == NULL || len == 0 || len > SIM_FRAME) {
        return -1;
    }
#if defined(__EMSCRIPTEN__)
    if (d->drop != 0) {
        d->drop--;
        return 0;
    }
    return pm_metal_drivers_net_sim_js_tx(frame, len);
#else
    uint32_t i;
    if (d->drop != 0) {
        d->drop--;
        return 0;
    }
    if (d->n >= SIM_Q) {
        return -1;
    }
    i = (d->head + d->n) % SIM_Q;
    memcpy(d->q[i], frame, len);
    d->ql[i] = len;
    d->n++;
    return 0;
#endif
}

static int32_t sim_poll(void *ctx) {
    struct sim_nic *d = ctx;
    uint32_t steps;
    if (d == NULL) {
        return -1;
    }
#if defined(__EMSCRIPTEN__)
    for (steps = 0; steps < 16u; steps++) {
        uint8_t frame[SIM_FRAME];
        int32_t n = pm_metal_drivers_net_sim_js_rx(frame, SIM_FRAME);
        if (n <= 0) {
            break;
        }
        (void)pm_metal_net_ip_rx_from(d->net_h, frame, (uint16_t)n);
    }
    return 0;
#else
    for (steps = 0; d->n != 0 && steps < 16u; steps++) {
        uint32_t i = d->head % SIM_Q;
        uint16_t n = d->ql[i];
        uint8_t frame[SIM_FRAME];
        memcpy(frame, d->q[i], n);
        d->head = (d->head + 1u) % SIM_Q;
        d->n--;
        (void)pm_metal_net_ip_rx_from(d->net_h, frame, n);
    }
    return 0;
#endif
}

static int32_t sim_attach(uint32_t unit) {
    uint32_t i;
    struct sim_nic *d;
    int32_t dt;
    if (s_arena == NULL) {
        return -1;
    }
    dt = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "sim", PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, unit);
    if (dt < 0) {
        return -1;
    }
    for (i = 0; i < SIM_MAX; i++) {
        if (s_dev[i].used && s_dev[i].dt_id == dt) {
            return s_dev[i].net_h;
        }
    }
    for (i = 0; i < SIM_MAX; i++) {
        if (s_dev[i].used) {
            continue;
        }
        d = &s_dev[i];
        memset(d, 0, sizeof(*d));
        d->used = 1;
        d->mac[0] = 0x02;
        d->mac[5] = (uint8_t)(0x03u + i);
        d->ops.open = sim_open;
        d->ops.close = sim_close;
        d->ops.mac = sim_mac;
        d->ops.tx = sim_tx;
        d->ops.poll = sim_poll;
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

int32_t pm_metal_drivers_net_sim_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_dev, 0, sizeof(s_dev));
    return 0;
}

void pm_metal_drivers_net_sim_deinit(void) {
    memset(s_dev, 0, sizeof(s_dev));
    s_arena = NULL;
}

int32_t pm_metal_drivers_net_sim_probe(void) {
    uint32_t i;
    for (i = 0; i < SIM_MAX; i++) {
        if (!s_dev[i].used) {
            return sim_attach(i);
        }
    }
    return -1;
}

int32_t pm_metal_drivers_net_sim_up(void) {
    uint32_t i;
    if (s_arena == NULL) {
        return -1;
    }
    for (i = 0; i < SIM_MAX; i++) {
        if (s_dev[i].used) {
            return 0;
        }
    }
    return sim_attach(0) >= 0 ? 0 : -1;
}

int32_t pm_metal_drivers_net_sim_drop(uint32_t n) {
    uint32_t i;
    for (i = 0; i < SIM_MAX; i++) {
        if (s_dev[i].used) {
            s_dev[i].drop = n;
        }
    }
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.sim, pm_metal_drivers_net_sim_init, pm_metal_drivers_net_sim_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.sim, pm_metal_drivers_net_sim_deinit, pm_metal_drivers_net_sim_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.sim, pm_metal_drivers_net_sim_probe, pm_metal_drivers_net_sim_probe, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.sim, pm_metal_drivers_net_sim_up, pm_metal_drivers_net_sim_up, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.sim, pm_metal_drivers_net_sim_drop, pm_metal_drivers_net_sim_drop, int32_t(uint32_t));

PM_MOD_BOOT_C(pymergetic.metal.drivers.net.sim, pm_metal_drivers_net_sim_init, pm_metal_drivers_net_sim_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.net.sim, pymergetic.metal.drivers.net);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.net.sim, pymergetic.metal.net.ip);

static int32_t sim_drv_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2,
    uint32_t loc3) {
    (void)bus;
    (void)loc0;
    (void)loc1;
    (void)loc2;
    return sim_attach(loc3) >= 0 ? 0 : -1;
}

#include "pymergetic/metal/drivers/__types__.h"

PM_METAL_DRV_PLATFORM_C(pymergetic.metal.drivers.net.sim, sim_drv_attach);
