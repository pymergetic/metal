/* pymergetic.metal.drivers.net — netdev table. Drivers bind; ip attaches handles. */
#include "pymergetic/metal/drivers/net/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/__types__.h"
#include "pymergetic/metal/net/ip.h"

#include <string.h>

#define PM_METAL_NETDEV_MAX 32u

struct pm_metal_netdev {
    uint32_t used;
    int32_t dt_id;
    /* Frames the driver took and frames it refused. A wire that carries nothing
     * looks exactly like a wire nobody used until someone counts. */
    uint32_t tx_n;
    uint32_t tx_err;
    uint32_t rx_n;
    pm_metal_netdev_ops_t ops;
};

static pm_util_mem_arena_t *s_arena;
static struct pm_metal_netdev s_dev[PM_METAL_NETDEV_MAX];

int32_t pm_metal_drivers_net_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_dev, 0, sizeof(s_dev));
    return 0;
}

void pm_metal_drivers_net_deinit(void) {
    uint32_t i;
    for (i = 0; i < PM_METAL_NETDEV_MAX; i++) {
        if (s_dev[i].used && s_dev[i].ops.close != NULL) {
            s_dev[i].ops.close(s_dev[i].ops.ctx);
        }
    }
    memset(s_dev, 0, sizeof(s_dev));
    s_arena = NULL;
}

int32_t pm_metal_drivers_net_bind(int32_t dt_id, const pm_metal_netdev_ops_t *ops) {
    uint32_t i;
    int32_t st;
    if (s_arena == NULL || dt_id < 0 || ops == NULL) {
        return -1;
    }
    for (i = 0; i < PM_METAL_NETDEV_MAX; i++) {
        if (s_dev[i].used && s_dev[i].dt_id == dt_id) {
            return (int32_t)i;
        }
    }
    for (i = 0; i < PM_METAL_NETDEV_MAX; i++) {
        if (!s_dev[i].used) {
            s_dev[i].ops = *ops;
            if (s_dev[i].ops.open != NULL) {
                st = s_dev[i].ops.open(s_dev[i].ops.ctx);
                if (st != 0) {
                    memset(&s_dev[i], 0, sizeof(s_dev[i]));
                    return st;
                }
            }
            s_dev[i].used = 1;
            s_dev[i].dt_id = dt_id;
            return (int32_t)i;
        }
    }
    return -1;
}

int32_t pm_metal_drivers_net_unbind(int32_t h) {
    if (h < 0 || (uint32_t)h >= PM_METAL_NETDEV_MAX || !s_dev[h].used) {
        return -1;
    }
    if (s_dev[h].ops.close != NULL) {
        s_dev[h].ops.close(s_dev[h].ops.ctx);
    }
    memset(&s_dev[h], 0, sizeof(s_dev[h]));
    return 0;
}

int32_t pm_metal_drivers_net_unbind_dt(int32_t dt_id) {
    uint32_t i;
    int32_t st = -1;
    for (i = 0; i < PM_METAL_NETDEV_MAX; i++) {
        if (s_dev[i].used && s_dev[i].dt_id == dt_id) {
            st = pm_metal_drivers_net_unbind((int32_t)i);
        }
    }
    return st;
}

int32_t pm_metal_drivers_net_count(void) {
    uint32_t i;
    int32_t n = 0;
    for (i = 0; i < PM_METAL_NETDEV_MAX; i++) {
        if (s_dev[i].used) {
            n++;
        }
    }
    return n;
}

int32_t pm_metal_drivers_net_dt_id(int32_t h) {
    if (h < 0 || (uint32_t)h >= PM_METAL_NETDEV_MAX || !s_dev[h].used) {
        return -1;
    }
    return s_dev[h].dt_id;
}

int32_t pm_metal_drivers_net_by_dt(int32_t dt_id) {
    uint32_t i;
    if (dt_id < 0) {
        return -1;
    }
    for (i = 0; i < PM_METAL_NETDEV_MAX; i++) {
        if (s_dev[i].used && s_dev[i].dt_id == dt_id) {
            return (int32_t)i;
        }
    }
    return -1;
}

int32_t pm_metal_drivers_net_by_compat(const char *compat, int32_t nth) {
    uint32_t i;
    int32_t seen = 0;
    const char *c;
    if (compat == NULL || compat[0] == 0 || nth < 0) {
        return -1;
    }
    for (i = 0; i < PM_METAL_NETDEV_MAX; i++) {
        if (!s_dev[i].used) {
            continue;
        }
        c = pm_metal_dt_compat(s_dev[i].dt_id);
        if (c == NULL || strcmp(c, compat) != 0) {
            continue;
        }
        if (seen == nth) {
            return (int32_t)i;
        }
        seen++;
    }
    return -1;
}

int32_t pm_metal_drivers_net_poll(int32_t h) {
    if (h < 0 || (uint32_t)h >= PM_METAL_NETDEV_MAX || !s_dev[h].used) {
        return -1;
    }
    if (s_dev[h].ops.poll == NULL) {
        return 0;
    }
    return s_dev[h].ops.poll(s_dev[h].ops.ctx);
}

int32_t pm_metal_drivers_net_poll_all(void) {
    uint32_t i;
    for (i = 0; i < PM_METAL_NETDEV_MAX; i++) {
        if (s_dev[i].used && s_dev[i].ops.poll != NULL) {
            (void)s_dev[i].ops.poll(s_dev[i].ops.ctx);
        }
    }
    return 0;
}

int32_t pm_metal_drivers_net_tx(int32_t h, const uint8_t *frame, uint16_t len) {
    if (h < 0 || (uint32_t)h >= PM_METAL_NETDEV_MAX || !s_dev[h].used || s_dev[h].ops.tx == NULL) {
        return -1;
    }
    {
        int32_t st = s_dev[h].ops.tx(s_dev[h].ops.ctx, frame, len);
        if (st == 0) {
            s_dev[h].tx_n++;
        } else {
            s_dev[h].tx_err++;
        }
        return st;
    }
}

void pm_metal_drivers_net_count_rx(int32_t h) {
    if (h < 0 || (uint32_t)h >= PM_METAL_NETDEV_MAX || !s_dev[h].used) {
        return;
    }
    s_dev[h].rx_n++;
}

uint32_t pm_metal_drivers_net_tx_n(int32_t h) {
    if (h < 0 || (uint32_t)h >= PM_METAL_NETDEV_MAX || !s_dev[h].used) {
        return 0;
    }
    return s_dev[h].tx_n;
}

uint32_t pm_metal_drivers_net_tx_err(int32_t h) {
    if (h < 0 || (uint32_t)h >= PM_METAL_NETDEV_MAX || !s_dev[h].used) {
        return 0;
    }
    return s_dev[h].tx_err;
}

uint32_t pm_metal_drivers_net_rx_n(int32_t h) {
    if (h < 0 || (uint32_t)h >= PM_METAL_NETDEV_MAX || !s_dev[h].used) {
        return 0;
    }
    return s_dev[h].rx_n;
}

void pm_metal_drivers_net_mac(int32_t h, uint8_t *out) {
    if (out == NULL) {
        return;
    }
    memset(out, 0, 6);
    if (h < 0 || (uint32_t)h >= PM_METAL_NETDEV_MAX || !s_dev[h].used || s_dev[h].ops.mac == NULL) {
        return;
    }
    s_dev[h].ops.mac(s_dev[h].ops.ctx, out);
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_init, pm_metal_drivers_net_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_deinit, pm_metal_drivers_net_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_bind, pm_metal_drivers_net_bind, int32_t(int32_t, const pm_metal_netdev_ops_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_unbind, pm_metal_drivers_net_unbind, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_unbind_dt, pm_metal_drivers_net_unbind_dt, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_count, pm_metal_drivers_net_count, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_dt_id, pm_metal_drivers_net_dt_id, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_by_dt, pm_metal_drivers_net_by_dt, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_by_compat, pm_metal_drivers_net_by_compat, int32_t(const char *, int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_poll, pm_metal_drivers_net_poll, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_poll_all, pm_metal_drivers_net_poll_all, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_tx, pm_metal_drivers_net_tx, int32_t(int32_t, const uint8_t *, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_mac, pm_metal_drivers_net_mac, void(int32_t, uint8_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_count_rx, pm_metal_drivers_net_count_rx, void(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_tx_n, pm_metal_drivers_net_tx_n, uint32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_tx_err, pm_metal_drivers_net_tx_err, uint32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_rx_n, pm_metal_drivers_net_rx_n, uint32_t(int32_t));

PM_MOD_BOOT_C(pymergetic.metal.drivers.net, pm_metal_drivers_net_init, pm_metal_drivers_net_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.net, pymergetic.metal.drivers);

static int32_t net_class_unbind(int32_t dt_id) {
    int32_t h = pm_metal_drivers_net_by_dt(dt_id);
    if (h >= 0) {
        (void)pm_metal_net_ip_l2_detach(h);
    }
    return pm_metal_drivers_net_unbind_dt(dt_id);
}

PM_METAL_CLASS_C(PM_METAL_DT_CLASS_NET, net_class_unbind);
