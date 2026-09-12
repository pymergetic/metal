/* pymergetic.metal.drivers.net.tap — instanced unix TAP (IFF_TAP, IFF_NO_PI). */
#define _GNU_SOURCE
#include "pymergetic/metal/drivers/net/tap/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/util/limits/__types__.h"
#include "pymergetic/util/mem.h"

#include <string.h>

#if defined(__linux__) && !defined(PM_METAL_FIRMWARE)
#include <errno.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

/* The kernel holds this NIC's queue, so a tap row costs one frame to read
 * into — taken when the NIC attaches, at the size the frame knob says. */
#define TAP_DEVICE_DEFAULT 2u
#define TAP_FRAME_DEFAULT 2048u

struct tap_nic {
    uint32_t used;
    int fd;
    uint8_t mac[6];
    uint8_t *rx;
    uint32_t qframe;
    uint32_t unit;
    int32_t dt_id;
    int32_t net_h;
    pm_metal_netdev_ops_t ops;
    struct tap_nic *next;
};

static pm_util_mem_arena_t *s_arena;
/* One row per NIC, taken when it attaches and kept for the seat. The netdev
 * core holds each row's address as its ops ctx, so rows are linked, never
 * moved. */
static struct tap_nic *s_head;
static uint32_t s_dev_used;

PM_UTIL_LIMIT_C(pm_metal_net_tap_limit_device, pymergetic.metal.drivers.net.tap, device,
    TAP_DEVICE_DEFAULT, 0u, &s_dev_used);
PM_UTIL_LIMIT_C(pm_metal_net_tap_limit_frame, pymergetic.metal.drivers.net.tap, frame,
    TAP_FRAME_DEFAULT, 0u, NULL);

/* The frame this NIC reads into, at the size the knob says now. Kept across a
 * close/attach cycle when it already matches. */
static int32_t tap_frame_fit(struct tap_nic *d) {
    uint32_t qframe = pm_metal_net_tap_limit_frame.soft;
    uint8_t *rx;
    if (qframe == 0u) {
        qframe = pm_metal_net_tap_limit_frame.dflt;
    }
    if (d->rx != NULL && d->qframe == qframe) {
        return 0;
    }
    rx = pm_util_mem_alloc(s_arena, qframe);
    if (rx == NULL) {
        return -1;
    }
    d->rx = rx;
    d->qframe = qframe;
    return 0;
}

static int32_t tap_open(void *ctx) {
    struct tap_nic *d = ctx;
#if defined(__linux__) && !defined(PM_METAL_FIRMWARE)
    struct ifreq ifr;
    int fd;
    if (d == NULL) {
        return -1;
    }
    if (d->fd >= 0) {
        return 0;
    }
    fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        return 0;
    }
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = (short)(IFF_TAP | IFF_NO_PI);
    if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
        close(fd);
        return 0;
    }
    d->fd = fd;
    return 0;
#else
    (void)d;
    return 0;
#endif
}

static void tap_close(void *ctx) {
    struct tap_nic *d = ctx;
    if (d == NULL) {
        return;
    }
#if defined(__linux__) && !defined(PM_METAL_FIRMWARE)
    if (d->fd >= 0) {
        close(d->fd);
    }
#endif
    d->fd = -1;
    if (d->used != 0 && s_dev_used != 0) {
        s_dev_used--;
    }
    d->used = 0;
    d->dt_id = -1;
    d->net_h = -1;
}

static void tap_mac(void *ctx, uint8_t out[6]) {
    struct tap_nic *d = ctx;
    if (d == NULL) {
        memset(out, 0, 6);
        return;
    }
    memcpy(out, d->mac, 6);
}

static int32_t tap_tx(void *ctx, const uint8_t *frame, uint16_t len) {
    struct tap_nic *d = ctx;
#if defined(__linux__) && !defined(PM_METAL_FIRMWARE)
    ssize_t n;
    if (d == NULL || d->fd < 0 || frame == NULL || len == 0) {
        return -1;
    }
    n = write(d->fd, frame, len);
    return n == (ssize_t)len ? 0 : -1;
#else
    (void)d;
    (void)frame;
    (void)len;
    return -1;
#endif
}

static int32_t tap_poll(void *ctx) {
    struct tap_nic *d = ctx;
#if defined(__linux__) && !defined(PM_METAL_FIRMWARE)
    ssize_t n;
    if (d == NULL || d->fd < 0 || d->rx == NULL) {
        return 0;
    }
    n = read(d->fd, d->rx, d->qframe);
    if (n <= 0) {
        return 0;
    }
    (void)errno;
    return pm_metal_net_ip_rx_from(d->net_h, d->rx, (uint16_t)n);
#else
    (void)d;
    return 0;
#endif
}

/* A row for one more NIC: a closed one first, then a fresh one. Either way
 * the device knob says whether this card may offer another NIC at all — a
 * closed row is one this card already paid for, not a free pass over the
 * knob. NULL when it is already offering every NIC it may. */
static struct tap_nic *tap_row(void) {
    struct tap_nic *d = s_head;
    uint32_t rows = 0;
    if (!PM_UTIL_LIMIT_ROOM(pm_metal_net_tap_limit_device, s_dev_used)) {
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
    d->fd = -1;
    d->dt_id = -1;
    d->net_h = -1;
    d->next = s_head;
    s_head = d;
    return d;
}

static int32_t tap_attach(uint32_t unit) {
    struct tap_nic *d;
    int32_t dt;
    if (s_arena == NULL) {
        return -1;
    }
    dt = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "tap", PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, unit);
    if (dt < 0) {
        return -1;
    }
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used && d->dt_id == dt) {
            return d->net_h;
        }
    }
    d = tap_row();
    if (d == NULL) {
        return -1;
    }
    if (tap_frame_fit(d) != 0) {
        return -1;
    }
    d->fd = -1;
    d->mac[0] = 0x02;
    d->mac[5] = (uint8_t)(0x01u + d->unit);
    d->ops.open = tap_open;
    d->ops.close = tap_close;
    d->ops.mac = tap_mac;
    d->ops.tx = tap_tx;
    d->ops.poll = tap_poll;
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

int32_t pm_metal_drivers_net_tap_init(pm_util_mem_arena_t *arena) {
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

void pm_metal_drivers_net_tap_deinit(void) {
#if defined(__linux__) && !defined(PM_METAL_FIRMWARE)
    struct tap_nic *d;
    for (d = s_head; d != NULL; d = d->next) {
        if (d->fd >= 0) {
            close(d->fd);
            d->fd = -1;
        }
    }
#endif
    s_head = NULL;
    s_dev_used = 0;
    s_arena = NULL;
}

int32_t pm_metal_drivers_net_tap_probe(void) {
    struct tap_nic *d;
    uint32_t unit = 0;
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used) {
            unit++;
        }
    }
    return tap_attach(unit);
}

int32_t pm_metal_drivers_net_tap_up(void) {
    struct tap_nic *d;
    if (s_arena == NULL) {
        return -1;
    }
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used) {
            return 0;
        }
    }
    return tap_attach(0) >= 0 ? 0 : -1;
}

int32_t pm_metal_drivers_net_tap_fd(void) {
    struct tap_nic *d;
    for (d = s_head; d != NULL; d = d->next) {
        if (d->used && d->fd >= 0) {
            return d->fd;
        }
    }
    return -1;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.tap, pm_metal_drivers_net_tap_init, pm_metal_drivers_net_tap_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.tap, pm_metal_drivers_net_tap_deinit, pm_metal_drivers_net_tap_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.tap, pm_metal_drivers_net_tap_probe, pm_metal_drivers_net_tap_probe, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.tap, pm_metal_drivers_net_tap_up, pm_metal_drivers_net_tap_up, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.tap, pm_metal_drivers_net_tap_fd, pm_metal_drivers_net_tap_fd, int32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.drivers.net.tap, pm_metal_drivers_net_tap_init, pm_metal_drivers_net_tap_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.net.tap, pymergetic.metal.drivers.net);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.net.tap, pymergetic.metal.net.ip);
