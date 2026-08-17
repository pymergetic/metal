/* pymergetic.metal.net.dhcp — DISCOVER/OFFER on lo for the in-process server,
 * and the client's whole DORA exchange against a server on the far side of a
 * wire: broadcast out, broadcast back, options parsed, lease applied. */
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/net/dhcp.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LO4 0x7f000001u
#define OFFER 0x0a00000au
#define DHCP_PORT 67

#define SRV_ADDR 0x0a0b0001u   /* 10.11.0.1 — the server, and the router it hands out */
#define SRV_YIADDR 0x0a0b0064u /* 10.11.0.100 */
#define SRV_MASK 0xffffff00u
#define SRV_DNS 0x0a0b0002u
#define SRV_LEASE 3600u
#define BCAST 0xffffffffu
#define BOOT_MIN 240u
#define COOKIE 0x63825363u

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.dhcp test: %s\n", why);
    return 1;
}

/* ---- a DHCP server on the far side of the wire ---------------------------- */

#define SRV_FRAME 700

static struct {
    int32_t h;
    uint8_t q[SRV_FRAME];
    uint16_t qlen;
    uint32_t discovers;
    uint32_t requests;
    uint32_t requested_addr;
    uint32_t bad_src;
    pm_metal_netdev_ops_t ops;
} srv;

static const uint8_t srv_mac[6] = { 0x02u, 0, 0, 0, 0x0bu, 0x01u };
static const uint8_t cli_mac[6] = { 0x02u, 0, 0, 0, 0x0bu, 0x64u };

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint16_t sum16(const uint8_t *p, uint32_t n, uint32_t s) {
    while (n > 1u) {
        s += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        n -= 2;
    }
    if (n != 0) {
        s += (uint32_t)p[0] << 8;
    }
    while ((s >> 16) != 0) {
        s = (s & 0xffffu) + (s >> 16);
    }
    return (uint16_t)~s;
}

static uint8_t srv_opt_type(const uint8_t *p, uint32_t n) {
    uint32_t i = BOOT_MIN;
    while (i + 2u <= n && p[i] != 255u) {
        if (p[i] == 0) {
            i++;
            continue;
        }
        if ((uint32_t)p[i + 1u] + i + 2u > n) {
            break;
        }
        if (p[i] == 53u && p[i + 1u] >= 1u) {
            return p[i + 2u];
        }
        i += 2u + p[i + 1u];
    }
    return 0;
}

static uint32_t srv_opt_addr(const uint8_t *p, uint32_t n, uint8_t code) {
    uint32_t i = BOOT_MIN;
    while (i + 2u <= n && p[i] != 255u) {
        if (p[i] == 0) {
            i++;
            continue;
        }
        if ((uint32_t)p[i + 1u] + i + 2u > n) {
            break;
        }
        if (p[i] == code && p[i + 1u] == 4u) {
            return be32(p + i + 2u);
        }
        i += 2u + p[i + 1u];
    }
    return 0;
}

/* Broadcast reply: BOOTREPLY with the lease and the options we grant. */
static void srv_reply(const uint8_t *req, uint32_t reqlen, uint8_t type) {
    uint8_t *f = srv.q;
    uint8_t *boot;
    uint32_t at;
    uint32_t udp_len;
    uint32_t total;
    (void)reqlen;
    memset(f, 0, SRV_FRAME);
    memset(f, 0xff, 6);
    memcpy(f + 6, srv_mac, 6);
    f[12] = 0x08;
    f[13] = 0x00;
    boot = f + 14u + 20u + 8u;
    boot[0] = 2;
    boot[1] = 1;
    boot[2] = 6;
    memcpy(boot + 4, req + 4, 4); /* xid */
    wr32(boot + 16, SRV_YIADDR);
    wr32(boot + 20, SRV_ADDR);
    memcpy(boot + 28, req + 28, 6); /* chaddr */
    wr32(boot + 236, COOKIE);
    at = BOOT_MIN;
    boot[at++] = 53;
    boot[at++] = 1;
    boot[at++] = type;
    boot[at++] = 54;
    boot[at++] = 4;
    wr32(boot + at, SRV_ADDR);
    at += 4u;
    boot[at++] = 1;
    boot[at++] = 4;
    wr32(boot + at, SRV_MASK);
    at += 4u;
    boot[at++] = 3;
    boot[at++] = 4;
    wr32(boot + at, SRV_ADDR);
    at += 4u;
    boot[at++] = 6;
    boot[at++] = 4;
    wr32(boot + at, SRV_DNS);
    at += 4u;
    boot[at++] = 51;
    boot[at++] = 4;
    wr32(boot + at, SRV_LEASE);
    at += 4u;
    boot[at++] = 255;
    udp_len = 8u + at;
    total = 20u + udp_len;
    f[14] = 0x45;
    wr16(f + 14 + 2, (uint16_t)total);
    f[14 + 8] = 64;
    f[14 + 9] = 17;
    wr32(f + 14 + 12, SRV_ADDR);
    wr32(f + 14 + 16, BCAST);
    wr16(f + 14 + 10, sum16(f + 14, 20u, 0));
    wr16(f + 14 + 20, 67);
    wr16(f + 14 + 22, 68);
    wr16(f + 14 + 24, (uint16_t)udp_len);
    /* Leave the UDP checksum at 0: legal, and it keeps this fake server small. */
    srv.qlen = (uint16_t)(14u + total);
}

static int32_t srv_tx(void *ctx, const uint8_t *frame, uint16_t len) {
    const uint8_t *pkt;
    const uint8_t *boot;
    uint32_t total;
    uint32_t blen;
    (void)ctx;
    if (frame == NULL || len < 14u + 20u + 8u || rd16(frame + 12) != 0x0800u) {
        return 0;
    }
    pkt = frame + 14;
    if (pkt[9] != 17u) {
        return 0;
    }
    total = rd16(pkt + 2);
    if (total > (uint32_t)len - 14u || rd16(pkt + 20 + 2) != 67u) {
        return 0;
    }
    /* A client with no lease yet has no address to speak from. */
    if (be32(pkt + 12) != 0) {
        srv.bad_src++;
    }
    boot = pkt + 20u + 8u;
    blen = total - 28u;
    if (blen < BOOT_MIN || boot[0] != 1 || be32(boot + 236) != COOKIE) {
        return 0;
    }
    if (memcmp(boot + 28, cli_mac, 6) != 0) {
        srv.bad_src++;
    }
    if (srv_opt_type(boot, blen) == 1u) {
        srv.discovers++;
        srv_reply(boot, blen, 2u);
    } else if (srv_opt_type(boot, blen) == 3u) {
        srv.requests++;
        srv.requested_addr = srv_opt_addr(boot, blen, 50u);
        srv_reply(boot, blen, 5u);
    }
    return 0;
}

static int32_t srv_poll(void *ctx) {
    uint8_t f[SRV_FRAME];
    uint16_t n = srv.qlen;
    (void)ctx;
    if (n == 0) {
        return 0;
    }
    memcpy(f, srv.q, n);
    srv.qlen = 0;
    (void)pm_metal_net_ip_rx_from(srv.h, f, n);
    return 0;
}

static void srv_mac_of(void *ctx, uint8_t out[6]) {
    (void)ctx;
    memcpy(out, cli_mac, 6);
}

static int32_t srv_open(void *ctx) {
    (void)ctx;
    return 0;
}

static void srv_close(void *ctx) {
    (void)ctx;
}

static int32_t case_dora(void) {
    pm_metal_net_dhcp_lease_t lease;
    memset(&srv, 0, sizeof(srv));
    srv.h = -1;
    srv.ops.open = srv_open;
    srv.ops.close = srv_close;
    srv.ops.mac = srv_mac_of;
    srv.ops.tx = srv_tx;
    srv.ops.poll = srv_poll;
    if (pm_metal_net_l2_attach("dhcptest", &srv.ops) != 0) {
        return fail("dora attach");
    }
    srv.h = pm_metal_drivers_net_by_compat("dhcptest", 0);
    if (srv.h < 0) {
        return fail("dora handle");
    }
    memset(&lease, 0, sizeof(lease));
    if (pm_metal_net_dhcp_up(srv.h, &lease) != 0) {
        (void)pm_metal_drivers_net_unbind(srv.h);
        return fail("dora");
    }
    (void)pm_metal_drivers_net_unbind(srv.h);
    if (srv.discovers != 1u || srv.requests != 1u) {
        return fail("not a full exchange");
    }
    if (srv.requested_addr != SRV_YIADDR) {
        return fail("request asked for another address");
    }
    if (srv.bad_src != 0u) {
        return fail("client spoke from a borrowed address");
    }
    if (lease.addr_be != SRV_YIADDR || lease.mask_be != SRV_MASK) {
        return fail("lease address");
    }
    if (lease.gw_be != SRV_ADDR || lease.dns_be != SRV_DNS) {
        return fail("lease options");
    }
    if (lease.server_be != SRV_ADDR || lease.lease_sec != SRV_LEASE) {
        return fail("lease server");
    }
    if (pm_metal_net_ip_gw() != SRV_ADDR) {
        return fail("gateway not installed");
    }
    return 0;
}

int32_t pm_metal_net_dhcp_tests(void) {
    uint32_t yi = 0;
    if (pm_metal_net_dhcp_set_offer(OFFER) != 0) {
        return fail("offer");
    }
    if (pm_metal_net_dhcp_listen(LO4, DHCP_PORT) != 0) {
        return fail("listen");
    }
    if (pm_metal_net_dhcp_discover(LO4, DHCP_PORT, &yi) != 0) {
        return fail("discover");
    }
    if (yi != OFFER) {
        return fail("yiaddr");
    }
    if (pm_metal_net_ip_if_up(yi) != 0) {
        return fail("if_up");
    }
    if (case_dora() != 0) {
        return 1;
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.dhcp, tests, pm_metal_net_dhcp_tests);
