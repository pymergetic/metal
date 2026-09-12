/* pymergetic.metal.drivers.net.sim — instanced in-process L2.
 * Fill when probe bound no NIC (unix/emcc). Not a platform device next to virtio.
 * Browser: JS import in this card's library.js (metal.mk --js-library). */
#include "pymergetic/metal/drivers/net/sim/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/util/limits/__types__.h"
#include "pymergetic/util/mem.h"

#include <string.h>

#if defined(__EMSCRIPTEN__)
extern int32_t pm_metal_drivers_net_sim_js_tx(const uint8_t *frame, uint16_t len);
extern int32_t pm_metal_drivers_net_sim_js_rx(uint8_t *frame, uint16_t max);
#endif

/* What a NIC costs is its ring, and a ring is taken when a NIC attaches: the
 * table used to stand at 4 devices x 8 frames x 2KB in this seat's bss whether
 * or not anything ever bound. The three knobs are how many NICs this card
 * offers, how deep each one's ring is, and how big a frame on it may be. */
#define SIM_DEVICE_DEFAULT 4u
#define SIM_QUEUE_DEFAULT 8u
#define SIM_FRAME_DEFAULT 2048u

struct sim_nic {
    uint32_t used;
    uint32_t drop;
    uint8_t mac[6];
    /* The ring this NIC attached with: `qn` frames of `qframe` bytes back to
     * back, plus one more frame at `scratch` — poll hands a frame up while tx
     * may write into the slot it came from, so the frame it hands up is a
     * copy. Both live in the arena for as long as the row does. */
    uint8_t *q;
    uint8_t *scratch;
    uint16_t *ql;
    uint32_t qn;
    uint32_t qframe;
#if !defined(__EMSCRIPTEN__)
    uint32_t head;
    uint32_t n;
#endif
    uint32_t unit;
    int32_t dt_id;
    int32_t net_h;
    pm_metal_netdev_ops_t ops;
    struct sim_nic *next;
};

static pm_util_mem_arena_t *s_arena;
/* One row per NIC, taken when it attaches and kept for the seat (a closed NIC
 * leaves its row, and its ring, for the next one). The netdev core holds each
 * row's address as its ops ctx, so rows are linked, never moved. */
static struct sim_nic *s_head;
static uint32_t s_dev_used;

PM_UTIL_LIMIT_C(pm_metal_net_sim_limit_device, pymergetic.metal.drivers.net.sim, device,
    SIM_DEVICE_DEFAULT, 0u, &s_dev_used);
PM_UTIL_LIMIT_C(pm_metal_net_sim_limit_queue, pymergetic.metal.drivers.net.sim, queue,
    SIM_QUEUE_DEFAULT, 0u, NULL);
PM_UTIL_LIMIT_C(pm_metal_net_sim_limit_frame, pymergetic.metal.drivers.net.sim, frame,
    SIM_FRAME_DEFAULT, 0u, NULL);

#if !defined(__EMSCRIPTEN__)
/* The slot at `i` in this NIC's ring. */
static uint8_t *sim_slot(struct sim_nic *d, uint32_t i) {
    return d->q + (size_t)i * d->qframe;
}
#endif

static void sim_reset_q(struct sim_nic *d) {
#if !defined(__EMSCRIPTEN__)
    d->head = 0;
    d->n = 0;
#endif
    d->drop = 0;
}

/* This NIC's ring, at the depth and frame size the knobs say now. Kept across
 * a close/attach cycle when it already matches; taken from the arena when it
 * does not. Zero when the arena has nothing left — the attach then refuses
 * rather than binding a NIC with nowhere to put a frame. */
static int32_t sim_ring_fit(struct sim_nic *d) {
    uint32_t qn = pm_metal_net_sim_limit_queue.soft;
    uint32_t qframe = pm_metal_net_sim_limit_frame.soft;
    uint8_t *q;
    uint8_t *scratch;
    uint16_t *ql;
    if (qn == 0u) {
        qn = pm_metal_net_sim_limit_queue.dflt;
    }
    if (qframe == 0u) {
        qframe = pm_metal_net_sim_limit_frame.dflt;
    }
    if (d->scratch != NULL && d->qn == qn && d->qframe == qframe) {
        return 0;
    }
    /* On the browser seat the frames go through this card's JS tx/rx, so
     * there is no ring to hold them — only the frame poll hands up. */
#if defined(__EMSCRIPTEN__)
    q = NULL;
    ql = NULL;
#else
    q = pm_util_mem_alloc(s_arena, (size_t)qn * qframe);
    ql = pm_util_mem_alloc(s_arena, (size_t)qn * sizeof(*ql));
    if (q == NULL || ql == NULL) {
        return -1;
    }
#endif
    scratch = pm_util_mem_alloc(s_arena, qframe);
    if (scratch == NULL) {
        return -1;
    }
    d->q = q;
    d->scratch = scratch;
    d->ql = ql;
    d->qn = qn;
    d->qframe = qframe;
    return 0;
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
        if (d->used != 0 && s_dev_used != 0) {
            s_dev_used--;
        }
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
    if (d == NULL || frame == NULL || len == 0 || len > d->qframe) {
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
    if (d->n >= d->qn) {
        return -1;
    }
    i = (d->head + d->n) % d->qn;
    memcpy(sim_slot(d, i), frame, len);
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
    if (d->scratch == NULL) {
        return -1;
    }
#if defined(__EMSCRIPTEN__)
    for (steps = 0; steps < 16u; steps++) {
        int32_t n = pm_metal_drivers_net_sim_js_rx(d->scratch, (uint16_t)d->qframe);
        if (n <= 0) {
            break;
        }
        (void)pm_metal_net_ip_rx_from(d->net_h, d->scratch, (uint16_t)n);
    }
    return 0;
#else
    for (steps = 0; d->n != 0 && steps < 16u; steps++) {
        uint32_t i = d->head % d->qn;
        uint16_t n = d->ql[i];
        memcpy(d->scratch, sim_slot(d, i), n);
        d->head = (d->head + 1u) % d->qn;
        d->n--;
        (void)pm_metal_net_ip_rx_from(d->net_h, d->scratch, n);
    }
    return 0;
#endif
}

/* A row for one more NIC: a closed one first, then a fresh one. Either way
 * the device knob says whether this card may offer another NIC at all — a
 * closed row is one this card already paid for, not a free pass over the knob. NULL when this card is already offering every NIC it may, or when
 * the arena is out. */
static struct sim_nic *sim_row(void) {
    struct sim_nic *d = s_head;
    uint32_t rows = 0;
    if (!PM_UTIL_LIMIT_ROOM(pm_metal_net_sim_limit_device, s_dev_used)) {
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

static int32_t sim_attach(uint32_t unit) {
    struct sim_nic *d;
    int32_t dt;
    if (s_arena == NULL) {
        return -1;
    }
    dt = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "sim", PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, unit);
    if (dt < 0) {
        return -1;
    }
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used && d->dt_id == dt) {
            return d->net_h;
        }
    }
    d = sim_row();
    if (d == NULL) {
        return -1;
    }
    if (sim_ring_fit(d) != 0) {
        return -1;
    }
    sim_reset_q(d);
    d->mac[0] = 0x02;
    d->mac[5] = (uint8_t)(0x03u + d->unit);
    d->ops.open = sim_open;
    d->ops.close = sim_close;
    d->ops.mac = sim_mac;
    d->ops.tx = sim_tx;
    d->ops.poll = sim_poll;
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

int32_t pm_metal_drivers_net_sim_init(pm_util_mem_arena_t *arena) {
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

void pm_metal_drivers_net_sim_deinit(void) {
    s_head = NULL;
    s_dev_used = 0;
    s_arena = NULL;
}

int32_t pm_metal_drivers_net_sim_probe(void) {
    struct sim_nic *d;
    uint32_t unit = 0;
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used) {
            unit++;
        }
    }
    return sim_attach(unit);
}

int32_t pm_metal_drivers_net_sim_up(void) {
    struct sim_nic *d;
    if (s_arena == NULL) {
        return -1;
    }
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used) {
            return 0;
        }
    }
    return sim_attach(0) >= 0 ? 0 : -1;
}

/* What the ring of the NIC behind `net_h` was cut to, for the card's tests:
 * the knobs say what the next NIC takes, this says what a bound one has. */
int32_t pm_metal_drivers_net_sim_queue_of(int32_t net_h) {
    struct sim_nic *d;
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used && d->net_h == net_h) {
            return (int32_t)d->qn;
        }
    }
    return -1;
}

int32_t pm_metal_drivers_net_sim_frame_of(int32_t net_h) {
    struct sim_nic *d;
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used && d->net_h == net_h) {
            return (int32_t)d->qframe;
        }
    }
    return -1;
}

int32_t pm_metal_drivers_net_sim_drop(uint32_t n) {
    struct sim_nic *d;
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used) {
            d->drop = n;
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
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.sim, pm_metal_drivers_net_sim_queue_of, pm_metal_drivers_net_sim_queue_of, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.sim, pm_metal_drivers_net_sim_frame_of, pm_metal_drivers_net_sim_frame_of, int32_t(int32_t));

PM_MOD_BOOT_C(pymergetic.metal.drivers.net.sim, pm_metal_drivers_net_sim_init, pm_metal_drivers_net_sim_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.net.sim, pymergetic.metal.drivers.net);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.net.sim, pymergetic.metal.net.ip);
