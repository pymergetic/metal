/* pymergetic.metal.net.dhcp — DHCPv4. Two halves that must not be confused:
 * pm_metal_net_dhcp_lease is the client, and it does the whole DORA exchange
 * broadcast on a wire against whatever server answers; listen/poll/discover is
 * the in-process OFFER server, which only ever talks to this box. */
#include "pymergetic/metal/net/dhcp/__exports__.h"

#include "pymergetic/metal/async.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/net/dns.h"
#include "pymergetic/metal/net/ip.h"

#include <string.h>

#define BOOT_MIN 240u
#define COOKIE 0x63825363u
#define BCAST 0xffffffffu
#define DHCP_SERVER_PORT 67u
#define DHCP_CLIENT_PORT 68u
#define DHCP_DISCOVER 1u
#define DHCP_OFFER 2u
#define DHCP_REQUEST 3u
#define DHCP_ACK 5u
#define DHCP_NAK 6u
/* A spin cap for seats whose monotonic clock is a call counter rather than a
 * cycle counter. The wait per exchange leg is settable, because boot pays it
 * once per interface that has no server and a lab LAN is slower than slirp. */
#define DHCP_WAIT_DEFAULT_US 2000000ull
#define DHCP_SPINS 200000u
/* Interfaces whose address came from a server, so the box can say which. */
#define DHCP_HELD_MAX 8

static pm_util_mem_arena_t *s_arena;
static int32_t s_fd = -1;
static uint32_t s_offer_be = 0x0a000002u;
static uint32_t s_xid = 1;
static uint64_t s_wait_us = DHCP_WAIT_DEFAULT_US;

static struct {
    int32_t h;
    pm_metal_net_dhcp_lease_t lease;
} s_held[DHCP_HELD_MAX];

/* What happened the last time each interface asked. "Asked and got nothing" is
 * a different fact from "never asked", and the boot tree has to say which. */
#define DHCP_ASK_NONE 0
#define DHCP_ASK_SILENT 1
#define DHCP_ASK_LEASED 2

static struct {
    int32_t h;
    uint8_t how;
} s_ask[DHCP_HELD_MAX];

static void ask_note(int32_t h, uint8_t how) {
    uint32_t i;
    for (i = 0; i < DHCP_HELD_MAX; i++) {
        if (s_ask[i].how == DHCP_ASK_NONE || s_ask[i].h == h) {
            s_ask[i].h = h;
            s_ask[i].how = how;
            return;
        }
    }
}

static const pm_metal_net_dhcp_lease_t *held(int32_t h) {
    uint32_t i;
    for (i = 0; i < DHCP_HELD_MAX; i++) {
        if (s_held[i].h == h && s_held[i].lease.addr_be != 0) {
            return &s_held[i].lease;
        }
    }
    return NULL;
}

static void hold(int32_t h, const pm_metal_net_dhcp_lease_t *lease) {
    uint32_t i;
    for (i = 0; i < DHCP_HELD_MAX; i++) {
        if (s_held[i].lease.addr_be == 0 || s_held[i].h == h) {
            s_held[i].h = h;
            s_held[i].lease = *lease;
            return;
        }
    }
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint8_t opt_type(const uint8_t *p, uint32_t n) {
    uint32_t i = BOOT_MIN;
    while (i + 2u <= n && p[i] != 255) {
        if (p[i] == 0) {
            i++;
            continue;
        }
        uint8_t t = p[i];
        uint8_t l = p[i + 1];
        if (i + 2u + l > n) {
            break;
        }
        if (t == 53 && l >= 1) {
            return p[i + 2];
        }
        i += 2u + l;
    }
    return 0;
}

int32_t pm_metal_net_dhcp_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    s_fd = -1;
    s_offer_be = 0x0a000002u;
    s_wait_us = DHCP_WAIT_DEFAULT_US;
    memset(s_held, 0, sizeof(s_held));
    memset(s_ask, 0, sizeof(s_ask));
    return 0;
}

int32_t pm_metal_net_dhcp_set_wait_us(uint32_t us) {
    s_wait_us = us != 0 ? (uint64_t)us : DHCP_WAIT_DEFAULT_US;
    return 0;
}

void pm_metal_net_dhcp_deinit(void) {
    if (s_fd >= 0) {
        (void)pm_metal_net_ip_close(s_fd);
        s_fd = -1;
    }
    memset(s_held, 0, sizeof(s_held));
    memset(s_ask, 0, sizeof(s_ask));
    s_arena = NULL;
}

int32_t pm_metal_net_dhcp_set_offer(uint32_t addr_be) {
    if (addr_be == 0) {
        return -1;
    }
    s_offer_be = addr_be;
    return 0;
}

int32_t pm_metal_net_dhcp_listen(uint32_t addr_be, uint16_t port) {
    if (s_arena == NULL) {
        return -1;
    }
    if (s_fd >= 0) {
        return 0;
    }
    s_fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    if (s_fd < 0 || pm_metal_net_ip_bind(s_fd, addr_be, port) != 0) {
        if (s_fd >= 0) {
            (void)pm_metal_net_ip_close(s_fd);
            s_fd = -1;
        }
        return -1;
    }
    return 0;
}

int32_t pm_metal_net_dhcp_poll(void) {
    uint8_t buf[548];
    uint32_t src = 0;
    uint16_t sport = 0;
    int32_t n;
    if (s_fd < 0) {
        return 0;
    }
    n = pm_metal_net_ip_recvfrom(s_fd, buf, sizeof(buf), &src, &sport);
    if (n < (int32_t)BOOT_MIN || buf[0] != 1) {
        return 0;
    }
    if (opt_type(buf, (uint32_t)n) != 1) {
        return 0;
    }
    buf[0] = 2;
    put_be32(buf + 16, s_offer_be);
    buf[BOOT_MIN] = 53;
    buf[BOOT_MIN + 1] = 1;
    buf[BOOT_MIN + 2] = 2;
    buf[BOOT_MIN + 3] = 255;
    (void)pm_metal_net_ip_sendto(s_fd, buf, BOOT_MIN + 4u, src, sport);
    return 0;
}

int32_t pm_metal_net_dhcp_discover(uint32_t server_be, uint16_t server_port, uint32_t *yiaddr_be) {
    uint8_t q[BOOT_MIN + 4u];
    uint8_t buf[548];
    int32_t fd;
    int32_t n;
    uint32_t xid;
    if (yiaddr_be == NULL || s_arena == NULL) {
        return -1;
    }
    fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    if (fd < 0 || pm_metal_net_ip_bind(fd, 0x7f000001u, 68) != 0) {
        if (fd >= 0) {
            (void)pm_metal_net_ip_close(fd);
        }
        return -1;
    }
    memset(q, 0, sizeof(q));
    q[0] = 1;
    q[1] = 1;
    q[2] = 6;
    xid = s_xid++;
    put_be32(q + 4, xid);
    put_be32(q + 236, COOKIE);
    q[BOOT_MIN] = 53;
    q[BOOT_MIN + 1] = 1;
    q[BOOT_MIN + 2] = 1;
    q[BOOT_MIN + 3] = 255;
    if (pm_metal_net_ip_sendto(fd, q, sizeof(q), server_be, server_port) < 0) {
        (void)pm_metal_net_ip_close(fd);
        return -1;
    }
    (void)pm_metal_net_dhcp_poll();
    n = pm_metal_net_ip_recvfrom(fd, buf, sizeof(buf), NULL, NULL);
    (void)pm_metal_net_ip_close(fd);
    if (n < (int32_t)BOOT_MIN || buf[0] != 2 || get_be32(buf + 4) != xid) {
        return -1;
    }
    if (opt_type(buf, (uint32_t)n) != 2) {
        return -1;
    }
    *yiaddr_be = get_be32(buf + 16);
    return 0;
}

/* ---- client: the DORA exchange ------------------------------------------- */

static uint32_t opt_u32(const uint8_t *p, uint32_t n, uint8_t want) {
    uint32_t i = BOOT_MIN;
    while (i + 2u <= n && p[i] != 255) {
        if (p[i] == 0) {
            i++;
            continue;
        }
        uint8_t t = p[i];
        uint8_t l = p[i + 1];
        if (i + 2u + l > n) {
            break;
        }
        if (t == want && l == 4u) {
            return get_be32(p + i + 2u);
        }
        i += 2u + l;
    }
    return 0;
}

static uint32_t boot_head(uint8_t *q, uint32_t xid, const uint8_t mac[6], uint32_t ciaddr_be) {
    memset(q, 0, BOOT_MIN);
    q[0] = 1; /* BOOTREQUEST */
    q[1] = 1; /* ethernet */
    q[2] = 6; /* hlen */
    put_be32(q + 4, xid);
    /* Ask for broadcast replies: until the lease is applied this box has no
     * address a unicast answer could reach. */
    q[10] = 0x80;
    put_be32(q + 12, ciaddr_be);
    memcpy(q + 28, mac, 6);
    put_be32(q + 236, COOKIE);
    return BOOT_MIN;
}

static uint32_t opt_msg(uint8_t *q, uint32_t at, uint8_t type) {
    q[at] = 53;
    q[at + 1u] = 1;
    q[at + 2u] = type;
    return at + 3u;
}

static uint32_t opt_addr(uint8_t *q, uint32_t at, uint8_t code, uint32_t addr_be) {
    q[at] = code;
    q[at + 1u] = 4;
    put_be32(q + at + 2u, addr_be);
    return at + 6u;
}

static uint32_t opt_params(uint8_t *q, uint32_t at) {
    q[at] = 55;
    q[at + 1u] = 4;
    q[at + 2u] = 1;  /* subnet mask */
    q[at + 3u] = 3;  /* router */
    q[at + 4u] = 6;  /* dns */
    q[at + 5u] = 51; /* lease time */
    return at + 6u;
}

/* Wait for a reply of this type carrying our xid, driving the wire meanwhile. */
static int32_t await_reply(int32_t fd, uint32_t xid, uint8_t want, uint8_t *buf, uint32_t cap) {
    uint64_t deadline = pm_metal_async_mono_us() + s_wait_us;
    uint32_t spins;
    for (spins = 0; spins < DHCP_SPINS; spins++) {
        int32_t n = pm_metal_net_ip_recvfrom(fd, buf, cap, NULL, NULL);
        if (n >= (int32_t)BOOT_MIN && buf[0] == 2 && get_be32(buf + 4) == xid
            && get_be32(buf + 236) == COOKIE) {
            uint8_t t = opt_type(buf, (uint32_t)n);
            if (t == want) {
                return n;
            }
            if (t == DHCP_NAK) {
                return -1;
            }
        }
        if (pm_metal_async_mono_us() >= deadline) {
            break;
        }
        pm_metal_net_ip_pump();
    }
    return -1;
}

int32_t pm_metal_net_dhcp_lease(int32_t h, pm_metal_net_dhcp_lease_t *out) {
    uint8_t q[BOOT_MIN + 32u];
    uint8_t buf[548];
    uint8_t mac[6];
    int32_t fd;
    int32_t n;
    uint32_t at;
    uint32_t xid;
    uint32_t offer;
    uint32_t server;
    if (out == NULL || s_arena == NULL || pm_metal_drivers_net_dt_id(h) < 0) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    memset(mac, 0, sizeof(mac));
    pm_metal_drivers_net_mac(h, mac);
    /* The wire has to be part of the stack before it can carry the exchange that
     * gets it an address; a lease is asked for over an interface with none. */
    if (pm_metal_net_ip_l2_attach(h) != 0) {
        return -1;
    }
    fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    /* Bound to no address: the lease is what gives us one. Pinned to the
     * interface so a second NIC's traffic cannot answer for this one. */
    if (fd < 0 || pm_metal_net_ip_bind(fd, 0, DHCP_CLIENT_PORT) != 0
        || pm_metal_net_ip_bind_l2(fd, h) != 0) {
        if (fd >= 0) {
            (void)pm_metal_net_ip_close(fd);
        }
        return -1;
    }
    xid = ++s_xid;
    at = boot_head(q, xid, mac, 0);
    at = opt_msg(q, at, (uint8_t)DHCP_DISCOVER);
    at = opt_params(q, at);
    q[at++] = 255;
    if (pm_metal_net_ip_sendto(fd, q, at, BCAST, DHCP_SERVER_PORT) < 0) {
        (void)pm_metal_net_ip_close(fd);
        return -1;
    }
    n = await_reply(fd, xid, (uint8_t)DHCP_OFFER, buf, sizeof(buf));
    if (n < 0) {
        (void)pm_metal_net_ip_close(fd);
        return -1;
    }
    offer = get_be32(buf + 16);
    server = opt_u32(buf, (uint32_t)n, 54);
    if (offer == 0) {
        (void)pm_metal_net_ip_close(fd);
        return -1;
    }
    at = boot_head(q, xid, mac, 0);
    at = opt_msg(q, at, (uint8_t)DHCP_REQUEST);
    at = opt_addr(q, at, 50, offer);
    if (server != 0) {
        at = opt_addr(q, at, 54, server);
    }
    at = opt_params(q, at);
    q[at++] = 255;
    if (pm_metal_net_ip_sendto(fd, q, at, BCAST, DHCP_SERVER_PORT) < 0) {
        (void)pm_metal_net_ip_close(fd);
        return -1;
    }
    n = await_reply(fd, xid, (uint8_t)DHCP_ACK, buf, sizeof(buf));
    (void)pm_metal_net_ip_close(fd);
    if (n < 0) {
        return -1;
    }
    out->addr_be = get_be32(buf + 16);
    out->mask_be = opt_u32(buf, (uint32_t)n, 1);
    out->gw_be = opt_u32(buf, (uint32_t)n, 3);
    out->dns_be = opt_u32(buf, (uint32_t)n, 6);
    out->server_be = opt_u32(buf, (uint32_t)n, 54);
    out->lease_sec = opt_u32(buf, (uint32_t)n, 51);
    if (out->server_be == 0) {
        out->server_be = server;
    }
    return out->addr_be != 0 ? 0 : -1;
}

int32_t pm_metal_net_dhcp_up(int32_t h, pm_metal_net_dhcp_lease_t *out) {
    pm_metal_net_dhcp_lease_t lease;
    uint32_t mask;
    if (pm_metal_net_dhcp_lease(h, &lease) != 0) {
        ask_note(h, DHCP_ASK_SILENT);
        return -1;
    }
    /* A server that sends no mask leaves us to assume the classful one; /24 is
     * what every seat we boot on hands out. */
    mask = lease.mask_be != 0 ? lease.mask_be : 0xffffff00u;
    if (pm_metal_net_ip_if_up_mask(h, lease.addr_be, mask) != 0) {
        return -1;
    }
    if (lease.gw_be != 0 && pm_metal_net_ip_gw_set(lease.gw_be) != 0) {
        return -1;
    }
    if (lease.dns_be != 0) {
        (void)pm_metal_net_dns_server_set(lease.dns_be);
    }
    hold(h, &lease);
    ask_note(h, DHCP_ASK_LEASED);
    if (out != NULL) {
        *out = lease;
    }
    return 0;
}

int32_t pm_metal_net_dhcp_leased(int32_t h) {
    return held(h) != NULL ? 1 : 0;
}

int32_t pm_metal_net_dhcp_asked(int32_t h) {
    uint32_t i;
    for (i = 0; i < DHCP_HELD_MAX; i++) {
        if (s_ask[i].h == h && s_ask[i].how != DHCP_ASK_NONE) {
            return (int32_t)s_ask[i].how;
        }
    }
    return DHCP_ASK_NONE;
}

uint32_t pm_metal_net_dhcp_leased_gw(int32_t h) {
    const pm_metal_net_dhcp_lease_t *l = held(h);
    return l != NULL ? l->gw_be : 0;
}

uint32_t pm_metal_net_dhcp_leased_sec(int32_t h) {
    const pm_metal_net_dhcp_lease_t *l = held(h);
    return l != NULL ? l->lease_sec : 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_init, pm_metal_net_dhcp_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_lease, pm_metal_net_dhcp_lease, int32_t(int32_t, pm_metal_net_dhcp_lease_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_up, pm_metal_net_dhcp_up, int32_t(int32_t, pm_metal_net_dhcp_lease_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_deinit, pm_metal_net_dhcp_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_set_offer, pm_metal_net_dhcp_set_offer, int32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_listen, pm_metal_net_dhcp_listen, int32_t(uint32_t, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_poll, pm_metal_net_dhcp_poll, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_set_wait_us, pm_metal_net_dhcp_set_wait_us, int32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_asked, pm_metal_net_dhcp_asked, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_leased, pm_metal_net_dhcp_leased, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_leased_gw, pm_metal_net_dhcp_leased_gw, uint32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_leased_sec, pm_metal_net_dhcp_leased_sec, uint32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_discover, pm_metal_net_dhcp_discover, int32_t(uint32_t, uint16_t, uint32_t *));

PM_MOD_BOOT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_init, pm_metal_net_dhcp_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.dhcp, pymergetic.metal.net.ip);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.dhcp, pymergetic.metal.net.dns);
