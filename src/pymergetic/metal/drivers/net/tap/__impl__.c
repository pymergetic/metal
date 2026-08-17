/* pymergetic.metal.drivers.net.tap — instanced unix TAP (IFF_TAP, IFF_NO_PI). */
#define _GNU_SOURCE
#include "pymergetic/metal/drivers/net/tap/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/net/ip.h"

#include <string.h>

#if defined(__linux__) && !defined(PM_METAL_FIRMWARE)
#include <errno.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#define TAP_MAX 2u

struct tap_nic {
    uint32_t used;
    int fd;
    uint8_t mac[6];
    int32_t dt_id;
    int32_t net_h;
    pm_metal_netdev_ops_t ops;
};

static pm_util_mem_arena_t *s_arena;
static struct tap_nic s_dev[TAP_MAX];

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
    uint8_t buf[2048];
    ssize_t n;
    if (d == NULL || d->fd < 0) {
        return 0;
    }
    n = read(d->fd, buf, sizeof(buf));
    if (n <= 0) {
        return 0;
    }
    (void)errno;
    return pm_metal_net_ip_rx_from(d->net_h, buf, (uint16_t)n);
#else
    (void)d;
    return 0;
#endif
}

static int32_t tap_attach(uint32_t unit) {
    uint32_t i;
    struct tap_nic *d;
    int32_t dt;
    if (s_arena == NULL) {
        return -1;
    }
    dt = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "tap", PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, unit);
    if (dt < 0) {
        return -1;
    }
    for (i = 0; i < TAP_MAX; i++) {
        if (s_dev[i].used && s_dev[i].dt_id == dt) {
            return s_dev[i].net_h;
        }
    }
    for (i = 0; i < TAP_MAX; i++) {
        if (s_dev[i].used) {
            continue;
        }
        d = &s_dev[i];
        memset(d, 0, sizeof(*d));
        d->used = 1;
        d->fd = -1;
        d->mac[0] = 0x02;
        d->mac[5] = (uint8_t)(0x01u + i);
        d->ops.open = tap_open;
        d->ops.close = tap_close;
        d->ops.mac = tap_mac;
        d->ops.tx = tap_tx;
        d->ops.poll = tap_poll;
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

int32_t pm_metal_drivers_net_tap_init(pm_util_mem_arena_t *arena) {
    uint32_t i;
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_dev, 0, sizeof(s_dev));
    for (i = 0; i < TAP_MAX; i++) {
        s_dev[i].fd = -1;
    }
    return 0;
}

void pm_metal_drivers_net_tap_deinit(void) {
    uint32_t i;
#if defined(__linux__) && !defined(PM_METAL_FIRMWARE)
    for (i = 0; i < TAP_MAX; i++) {
        if (s_dev[i].fd >= 0) {
            close(s_dev[i].fd);
        }
    }
#else
    (void)i;
#endif
    memset(s_dev, 0, sizeof(s_dev));
    s_arena = NULL;
}

int32_t pm_metal_drivers_net_tap_probe(void) {
    uint32_t i;
    for (i = 0; i < TAP_MAX; i++) {
        if (!s_dev[i].used) {
            return tap_attach(i);
        }
    }
    return -1;
}

int32_t pm_metal_drivers_net_tap_up(void) {
    uint32_t i;
    if (s_arena == NULL) {
        return -1;
    }
    for (i = 0; i < TAP_MAX; i++) {
        if (s_dev[i].used) {
            return 0;
        }
    }
    return tap_attach(0) >= 0 ? 0 : -1;
}

int32_t pm_metal_drivers_net_tap_fd(void) {
    uint32_t i;
    for (i = 0; i < TAP_MAX; i++) {
        if (s_dev[i].used && s_dev[i].fd >= 0) {
            return s_dev[i].fd;
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
