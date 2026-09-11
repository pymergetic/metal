/* pymergetic.metal.net.ip — lo ping + UDP park prove, and one off-box prove
 * against a netdev that plays the far side of the wire: it answers ARP and
 * echo requests, and rejects any frame whose checksums do not add up. */
#include "pymergetic/metal/coop.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/util/limits.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LO4 0x7f000001u
#define PEER_IP 0x0a090001u /* 10.9.0.1 — our gateway and our ARP counterpart */
#define OUR_IP 0x0a090002u  /* 10.9.0.2/24 */
#define ONLINK_IP 0x0a090007u
#define FAR_IP 0x08080808u  /* off-subnet: reachable only through the gateway */

typedef struct {
    pm_metal_coop_coro_t coro;
    uint32_t step;
    int32_t fd;
    uint8_t buf[16];
    int32_t n;
} udp_rx_frame_t;

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.net.ip test: %s\n", why);
    return 1;
}

static int32_t case_ping_lo(void) {
    const uint8_t payload[] = { 'p', 'i', 'n', 'g' };
    uint8_t out[64];
    uint32_t n = sizeof(out);
    if (pm_metal_net_ip_ping4(LO4, payload, sizeof(payload), out, &n) != 0) {
        return fail("ping");
    }
    if (n < 8u + sizeof(payload) || out[0] != 0) {
        return fail("echo reply");
    }
    if (memcmp(out + 8, payload, sizeof(payload)) != 0) {
        return fail("payload");
    }
    return 0;
}

static int32_t case_udp_lo_sync(void) {
    int32_t a = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    int32_t b = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    if (a < 0 || b < 0) {
        return fail("socket");
    }
    if (pm_metal_net_ip_bind(a, LO4, 7001) != 0 || pm_metal_net_ip_bind(b, LO4, 7002) != 0) {
        return fail("bind");
    }
    const uint8_t msg[] = { 'h', 'i' };
    if (pm_metal_net_ip_sendto(a, msg, sizeof(msg), LO4, 7002) != (int32_t)sizeof(msg)) {
        return fail("sendto");
    }
    uint8_t buf[8];
    uint32_t addr = 0;
    uint16_t port = 0;
    int32_t n = pm_metal_net_ip_recvfrom(b, buf, sizeof(buf), &addr, &port);
    (void)pm_metal_net_ip_close(a);
    (void)pm_metal_net_ip_close(b);
    if (n != 2 || buf[0] != 'h' || port != 7001) {
        return fail("recvfrom");
    }
    return 0;
}

static pm_metal_coop_status_t step_udp_rx(pm_metal_coop_coro_t *self) {
    udp_rx_frame_t *f = (udp_rx_frame_t *)self;
    if (f->step == 0) {
        f->n = pm_metal_net_ip_recvfrom(f->fd, f->buf, sizeof(f->buf), NULL, NULL);
        if (f->n == 0) {
            f->step = 1;
            return PM_METAL_COOP_WAITING;
        }
        return f->n > 0 ? PM_METAL_COOP_DONE : PM_METAL_COOP_ERROR;
    }
    f->n = pm_metal_net_ip_recvfrom(f->fd, f->buf, sizeof(f->buf), NULL, NULL);
    return f->n > 0 ? PM_METAL_COOP_DONE : PM_METAL_COOP_ERROR;
}

static int32_t case_udp_park(void) {
    int32_t rx = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    int32_t tx = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    if (rx < 0 || tx < 0) {
        return fail("park socket");
    }
    if (pm_metal_net_ip_bind(rx, LO4, 7101) != 0 || pm_metal_net_ip_bind(tx, LO4, 7102) != 0) {
        return fail("park bind");
    }
    udp_rx_frame_t *f = (udp_rx_frame_t *)pm_metal_coop_coro_create(step_udp_rx, sizeof(*f));
    if (f == NULL) {
        return fail("park coro");
    }
    f->fd = rx;
    pm_metal_coop_task_t *t = pm_metal_coop_create_task(&f->coro);
    if (t == NULL) {
        return fail("park task");
    }
    /* First poll parks on empty recv. Under SMP a worker may claim the just-
     * pushed task first, so a single poll() can return before the step runs;
     * pump until the very first step has settled, then require it parked. */
    uint32_t settle = 0;
    while (f->coro.status == PM_METAL_COOP_PENDING && settle < 100000u) {
        pm_metal_coop_poll();
        settle++;
    }
    if (f->coro.status != PM_METAL_COOP_WAITING) {
        return fail("not parked");
    }
    const uint8_t msg[] = { 'z' };
    if (pm_metal_net_ip_sendto(tx, msg, 1, LO4, 7101) != 1) {
        return fail("park send");
    }
    if (pm_metal_coop_run(t) != 0) {
        return fail("park run");
    }
    (void)pm_metal_net_ip_close(rx);
    (void)pm_metal_net_ip_close(tx);
    if (f->n != 1 || f->buf[0] != 'z') {
        return fail("park data");
    }
    return 0;
}

static int32_t case_tcp_lo_echo(void) {
    int32_t ls = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    int32_t cl = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (ls < 0 || cl < 0) {
        return fail("tcp socket");
    }
    if (pm_metal_net_ip_bind(ls, LO4, 9000) != 0 || pm_metal_net_ip_listen(ls, 1) != 0) {
        return fail("tcp listen");
    }
    if (pm_metal_net_ip_connect(cl, LO4, 9000) != 1) {
        return fail("tcp connect");
    }
    int32_t sv = pm_metal_net_ip_accept(ls);
    if (sv < 0) {
        return fail("tcp accept");
    }
    const uint8_t msg[] = { 'a', 'b' };
    if (pm_metal_net_ip_send(cl, msg, sizeof(msg)) != 2) {
        return fail("tcp send");
    }
    uint8_t buf[8];
    int32_t n = pm_metal_net_ip_recv(sv, buf, sizeof(buf));
    if (n != 2 || buf[0] != 'a') {
        return fail("tcp recv");
    }
    if (pm_metal_net_ip_send(sv, buf, 2) != 2) {
        return fail("tcp reply");
    }
    n = pm_metal_net_ip_recv(cl, buf, sizeof(buf));
    (void)pm_metal_net_ip_close(cl);
    (void)pm_metal_net_ip_close(sv);
    (void)pm_metal_net_ip_close(ls);
    if (n != 2 || buf[1] != 'b') {
        return fail("tcp echo");
    }
    return 0;
}

typedef struct {
    pm_metal_coop_coro_t coro;
    uint32_t step;
    int32_t ls;
    int32_t acc;
} tcp_acc_frame_t;

static pm_metal_coop_status_t step_tcp_accept(pm_metal_coop_coro_t *self) {
    tcp_acc_frame_t *f = (tcp_acc_frame_t *)self;
    int32_t a = pm_metal_net_ip_accept(f->ls);
    if (a == -2) {
        f->step = 1;
        return PM_METAL_COOP_WAITING;
    }
    if (a < 0) {
        return PM_METAL_COOP_ERROR;
    }
    f->acc = a;
    return PM_METAL_COOP_DONE;
}

static int32_t case_tcp_accept_park(void) {
    int32_t ls = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    int32_t cl = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (ls < 0 || cl < 0) {
        return fail("acc socket");
    }
    if (pm_metal_net_ip_bind(ls, LO4, 9001) != 0 || pm_metal_net_ip_listen(ls, 1) != 0) {
        return fail("acc listen");
    }
    tcp_acc_frame_t *f = (tcp_acc_frame_t *)pm_metal_coop_coro_create(step_tcp_accept, sizeof(*f));
    if (f == NULL) {
        return fail("acc coro");
    }
    f->ls = ls;
    f->acc = -1;
    pm_metal_coop_task_t *t = pm_metal_coop_create_task(&f->coro);
    if (t == NULL) {
        return fail("acc task");
    }
    /* A fresh accept on an empty listener must park, not error or complete.
     * Poll until the coro's very first step has run (status leaves PENDING):
     * under SMP a worker may beat the caller to the just-pushed task, so a
     * single poll() can legitimately return before the step happens. The
     * invariant is the settle: once stepped, it must be parked (WAITING). */
    uint32_t settle = 0;
    while (f->coro.status == PM_METAL_COOP_PENDING && settle < 100000u) {
        pm_metal_coop_poll();
        settle++;
    }
    if (f->coro.status != PM_METAL_COOP_WAITING) {
        return fail("acc not parked");
    }
    if (pm_metal_net_ip_connect(cl, LO4, 9001) != 1) {
        return fail("acc connect");
    }
    if (pm_metal_coop_run(t) != 0 || f->acc < 0) {
        return fail("acc run");
    }
    (void)pm_metal_net_ip_close(f->acc);
    (void)pm_metal_net_ip_close(cl);
    (void)pm_metal_net_ip_close(ls);
    return 0;
}

/* ---- the wire's far side -------------------------------------------------- */

#define PEER_Q 4
#define PEER_FRAME 1600

static const uint8_t peer_mac[6] = { 0x02u, 0, 0, 0, 0, 0xbbu };
static const uint8_t our_mac[6] = { 0x02u, 0, 0, 0, 0, 0xaau };

static struct {
    int32_t h;
    uint8_t q[PEER_Q][PEER_FRAME];
    uint16_t ql[PEER_Q];
    uint32_t head;
    uint32_t n;
    /* Last IP frame we saw, plus what was wrong with anything we saw. */
    uint8_t last[PEER_FRAME];
    uint16_t last_len;
    uint32_t ip_seen;
    uint32_t arp_asked_for;
    uint32_t bad_csum;
    uint32_t bad_dmac;
    pm_metal_netdev_ops_t ops;
} peer;

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

/* Pseudo-header + transport bytes: zero means the checksum in the packet is
 * right, which is exactly what a real peer requires of us. */
static uint16_t l4_csum(const uint8_t *pkt, uint32_t total) {
    uint32_t ihl = (uint32_t)(pkt[0] & 0x0fu) * 4u;
    uint32_t s = 0;
    if (total < ihl + 8u) {
        return 0xffffu;
    }
    s = 0;
    s += (uint32_t)(0xffffu ^ sum16(pkt + 12, 8u, 0));
    s += (uint32_t)pkt[9];
    s += total - ihl;
    return sum16(pkt + ihl, total - ihl, s);
}

static void peer_queue(const uint8_t *frame, uint16_t len) {
    uint32_t i;
    if (peer.n >= PEER_Q || len > PEER_FRAME) {
        return;
    }
    i = (peer.head + peer.n) % PEER_Q;
    memcpy(peer.q[i], frame, len);
    peer.ql[i] = len;
    peer.n++;
}

static void peer_arp_reply(const uint8_t *req) {
    uint8_t f[42];
    memset(f, 0, sizeof(f));
    memcpy(f, req + 6, 6);
    memcpy(f + 6, peer_mac, 6);
    f[12] = 0x08;
    f[13] = 0x06;
    wr16(f + 14, 1);
    wr16(f + 16, 0x0800);
    f[18] = 6;
    f[19] = 4;
    wr16(f + 20, 2);
    memcpy(f + 22, peer_mac, 6);
    wr32(f + 28, PEER_IP);
    memcpy(f + 32, req + 22, 6);
    memcpy(f + 38, req + 28, 4);
    peer_queue(f, sizeof(f));
}

static void peer_echo_reply(const uint8_t *pkt, uint32_t total) {
    uint8_t f[PEER_FRAME];
    uint32_t ihl = (uint32_t)(pkt[0] & 0x0fu) * 4u;
    if (total + 14u > PEER_FRAME) {
        return;
    }
    memcpy(f, our_mac, 6);
    memcpy(f + 6, peer_mac, 6);
    f[12] = 0x08;
    f[13] = 0x00;
    memcpy(f + 14, pkt, total);
    wr32(f + 14 + 12, PEER_IP);
    wr32(f + 14 + 16, be32(pkt + 12));
    f[14 + 10] = 0;
    f[14 + 11] = 0;
    wr16(f + 14 + 10, sum16(f + 14, ihl, 0));
    f[14 + ihl] = 0; /* type 0 = echo reply */
    f[14 + ihl + 2] = 0;
    f[14 + ihl + 3] = 0;
    wr16(f + 14 + ihl + 2, sum16(f + 14 + ihl, total - ihl, 0));
    peer_queue(f, (uint16_t)(total + 14u));
}

static int32_t peer_tx(void *ctx, const uint8_t *frame, uint16_t len) {
    uint16_t et;
    (void)ctx;
    if (frame == NULL || len < 14u) {
        return -1;
    }
    et = rd16(frame + 12);
    if (et == 0x0806u && len >= 42u && rd16(frame + 20) == 1u) {
        peer.arp_asked_for = be32(frame + 38);
        if (peer.arp_asked_for == PEER_IP) {
            peer_arp_reply(frame);
        }
        return 0;
    }
    if (et != 0x0800u || len < 14u + 20u) {
        return 0;
    }
    {
        const uint8_t *pkt = frame + 14;
        uint32_t total = rd16(pkt + 2);
        if (total > (uint32_t)len - 14u) {
            total = (uint32_t)len - 14u;
        }
        peer.ip_seen++;
        memcpy(peer.last, frame, len);
        peer.last_len = len;
        if (sum16(pkt, (uint32_t)(pkt[0] & 0x0fu) * 4u, 0) != 0) {
            peer.bad_csum++;
        }
        if ((pkt[9] == 6u || pkt[9] == 17u) && l4_csum(pkt, total) != 0) {
            peer.bad_csum++;
        }
        if (memcmp(frame, peer_mac, 6) != 0) {
            peer.bad_dmac++;
        }
        if (pkt[9] == 1u && total >= 28u && pkt[(pkt[0] & 0x0fu) * 4u] == 8u
            && be32(pkt + 16) == PEER_IP) {
            peer_echo_reply(pkt, total);
        }
    }
    return 0;
}

static int32_t peer_poll(void *ctx) {
    uint32_t steps;
    (void)ctx;
    for (steps = 0; peer.n != 0 && steps < 8u; steps++) {
        uint32_t i = peer.head % PEER_Q;
        uint8_t f[PEER_FRAME];
        uint16_t n = peer.ql[i];
        memcpy(f, peer.q[i], n);
        peer.head = (peer.head + 1u) % PEER_Q;
        peer.n--;
        (void)pm_metal_net_ip_rx_from(peer.h, f, n);
    }
    return 0;
}

static void peer_mac_of(void *ctx, uint8_t out[6]) {
    (void)ctx;
    memcpy(out, our_mac, 6);
}

static int32_t peer_open(void *ctx) {
    (void)ctx;
    return 0;
}

static void peer_close(void *ctx) {
    (void)ctx;
}

static int32_t peer_up(void) {
    memset(&peer, 0, sizeof(peer));
    peer.h = -1;
    peer.ops.open = peer_open;
    peer.ops.close = peer_close;
    peer.ops.mac = peer_mac_of;
    peer.ops.tx = peer_tx;
    peer.ops.poll = peer_poll;
    if (pm_metal_net_l2_attach("iptest", &peer.ops) != 0) {
        return -1;
    }
    peer.h = pm_metal_drivers_net_by_compat("iptest", 0);
    if (peer.h < 0) {
        return -1;
    }
    if (pm_metal_net_ip_if_up_h(peer.h, OUR_IP) != 0) {
        return -1;
    }
    return pm_metal_net_ip_gw_set(PEER_IP);
}

static void peer_down(void) {
    if (peer.h >= 0) {
        (void)pm_metal_drivers_net_unbind(peer.h);
        peer.h = -1;
    }
}

static void pump_n(uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        pm_metal_net_ip_pump();
    }
}

/* One datagram to the gateway: it must be held for ARP, asked for by ARP, and
 * then sent to the MAC that answered — never to a guessed one. */
static int32_t case_arp_then_send(void) {
    int32_t fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    const uint8_t msg[] = { 'q' };
    if (fd < 0 || pm_metal_net_ip_bind(fd, OUR_IP, 7201) != 0) {
        return fail("offbox bind");
    }
    peer.ip_seen = 0;
    peer.arp_asked_for = 0;
    if (pm_metal_net_ip_sendto(fd, msg, sizeof(msg), PEER_IP, 9999) != 1) {
        return fail("offbox sendto");
    }
    if (peer.ip_seen != 0) {
        return fail("sent before arp");
    }
    if (peer.arp_asked_for != PEER_IP) {
        return fail("no arp request");
    }
    pump_n(4);
    (void)pm_metal_net_ip_close(fd);
    if (peer.ip_seen != 1u) {
        return fail("queued datagram never sent");
    }
    if (peer.bad_dmac != 0) {
        return fail("wrong dmac");
    }
    if (peer.bad_csum != 0) {
        return fail("udp checksum");
    }
    if (pm_metal_net_ip_arp_resolve(PEER_IP) != 1) {
        return fail("arp not cached");
    }
    return 0;
}

/* On-link destinations are resolved themselves; off-subnet ones go to the
 * gateway's MAC while keeping their own IP destination. */
static int32_t case_nexthop(void) {
    int32_t fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    const uint8_t msg[] = { 'n' };
    if (fd < 0 || pm_metal_net_ip_bind(fd, OUR_IP, 7202) != 0) {
        return fail("nexthop bind");
    }
    peer.arp_asked_for = 0;
    (void)pm_metal_net_ip_sendto(fd, msg, sizeof(msg), ONLINK_IP, 9999);
    if (peer.arp_asked_for != ONLINK_IP) {
        return fail("on-link asked for gateway");
    }
    peer.ip_seen = 0;
    peer.arp_asked_for = 0;
    if (pm_metal_net_ip_sendto(fd, msg, sizeof(msg), FAR_IP, 9999) != 1) {
        return fail("far sendto");
    }
    (void)pm_metal_net_ip_close(fd);
    if (peer.arp_asked_for != 0) {
        return fail("asked arp for a far address");
    }
    if (peer.ip_seen != 1u || be32(peer.last + 14 + 16) != FAR_IP) {
        return fail("far datagram");
    }
    if (memcmp(peer.last, peer_mac, 6) != 0) {
        return fail("far dmac not gateway");
    }
    return 0;
}

static int32_t case_tcp_offbox_csum(void) {
    int32_t fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (fd < 0) {
        return fail("tcp offbox socket");
    }
    peer.ip_seen = 0;
    peer.bad_csum = 0;
    if (pm_metal_net_ip_connect(fd, PEER_IP, 80) != 0) {
        return fail("tcp offbox connect");
    }
    (void)pm_metal_net_ip_close(fd);
    if (peer.ip_seen == 0u) {
        return fail("no syn on the wire");
    }
    if (peer.bad_csum != 0) {
        return fail("tcp checksum");
    }
    if (peer.last[14 + 9] != 6u || rd16(peer.last + 14 + 20 + 2) != 80u) {
        return fail("syn shape");
    }
    return 0;
}

static int32_t case_ping_offbox(void) {
    const uint8_t payload[] = { 'o', 'f', 'f' };
    uint8_t out[64];
    uint32_t n = sizeof(out);
    if (pm_metal_net_ip_ping4(PEER_IP, payload, sizeof(payload), out, &n) != 0) {
        return fail("offbox ping");
    }
    if (n < 8u + sizeof(payload) || out[0] != 0) {
        return fail("offbox echo reply");
    }
    if (memcmp(out + 8, payload, sizeof(payload)) != 0) {
        return fail("offbox payload");
    }
    return 0;
}

/* A packet whose transport checksum is wrong must not reach a socket: without
 * this the stack would accept its own corruption as data. */
static int32_t case_csum_drop(void) {
    int32_t fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    uint8_t f[14u + 20u + 8u + 1u];
    uint8_t buf[8];
    uint32_t total = 20u + 8u + 1u;
    if (fd < 0 || pm_metal_net_ip_bind(fd, OUR_IP, 7203) != 0) {
        return fail("csum bind");
    }
    memset(f, 0, sizeof(f));
    memcpy(f, our_mac, 6);
    memcpy(f + 6, peer_mac, 6);
    f[12] = 0x08;
    f[13] = 0x00;
    f[14] = 0x45;
    wr16(f + 14 + 2, (uint16_t)total);
    f[14 + 8] = 64;
    f[14 + 9] = 17;
    wr32(f + 14 + 12, PEER_IP);
    wr32(f + 14 + 16, OUR_IP);
    wr16(f + 14 + 10, sum16(f + 14, 20u, 0));
    wr16(f + 14 + 20, 9999);
    wr16(f + 14 + 22, 7203);
    wr16(f + 14 + 24, 9);
    f[14 + 28] = 'x';
    /* l4_csum over a zeroed field is the value to write; over a whole packet it
     * is 0 when that value is right. One flipped bit makes it wrong. */
    wr16(f + 14 + 26, l4_csum(f + 14, total));
    wr16(f + 14 + 26, (uint16_t)(rd16(f + 14 + 26) ^ 0x0001u));
    (void)pm_metal_net_ip_rx_from(peer.h, f, (uint16_t)sizeof(f));
    if (pm_metal_net_ip_recvfrom(fd, buf, sizeof(buf), NULL, NULL) > 0) {
        return fail("bad checksum accepted");
    }
    wr16(f + 14 + 26, 0);
    wr16(f + 14 + 26, l4_csum(f + 14, total));
    (void)pm_metal_net_ip_rx_from(peer.h, f, (uint16_t)sizeof(f));
    {
        int32_t got = pm_metal_net_ip_recvfrom(fd, buf, sizeof(buf), NULL, NULL);
        (void)pm_metal_net_ip_close(fd);
        if (got != 1 || buf[0] != 'x') {
            return fail("good checksum dropped");
        }
    }
    return 0;
}

static int32_t case_multicast_lo(void) {
#define MGROUP 0xef000001u /* 239.0.0.1 */
    int32_t sub = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    int32_t non = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    int32_t tx = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    uint8_t buf[8];
    uint32_t addr = 0;
    uint16_t port = 0;
    int32_t n;
    if (sub < 0 || non < 0 || tx < 0) {
        return fail("mcast socket");
    }
    /* The group listener binds the wildcard address (like a scout socket) so it
     * can receive group traffic on any interface; both sub and non take the same
     * port so "did the join gate delivery" is the only difference. */
    if (pm_metal_net_ip_bind(sub, 0, 7300) != 0 || pm_metal_net_ip_bind(non, 0, 7300) != 0 ||
        pm_metal_net_ip_bind(tx, LO4, 7301) != 0) {
        return fail("mcast bind");
    }
    if (pm_metal_net_ip_join_group(sub, MGROUP) != 0) {
        return fail("mcast join");
    }
    /* Before the sender publishes, the non-member already tried to read and must
     * stay empty: nothing has arrived, so recvfrom returns 0 (nothing queued). */
    const uint8_t msg[] = { 'g', 'r', 'p' };
    if (pm_metal_net_ip_sendto(tx, msg, sizeof(msg), MGROUP, 7300) != (int32_t)sizeof(msg)) {
        return fail("mcast sendto");
    }
    n = pm_metal_net_ip_recvfrom(non, buf, sizeof(buf), &addr, &port);
    if (n != 0) {
        return fail("mcast leaked to non-member");
    }
    n = pm_metal_net_ip_recvfrom(sub, buf, sizeof(buf), &addr, &port);
    (void)pm_metal_net_ip_leave_group(sub, MGROUP);
    (void)pm_metal_net_ip_leave_group(non, MGROUP);
    (void)pm_metal_net_ip_close(sub);
    (void)pm_metal_net_ip_close(non);
    (void)pm_metal_net_ip_close(tx);
    if (n != (int32_t)sizeof(msg) || buf[0] != 'g' || buf[2] != 'p' || addr != LO4 || port != 7301) {
        return fail("mcast recvfrom");
    }
    return 0;
}

/* A listener holds as many completed connections as it asked listen() for.
 *
 * This seat serves pages with no keep-alive, so a browser opens one connection
 * per asset and a single reload arrives as six at once, the console panel
 * polling beside them. Every listener used to hold four whatever it asked for,
 * and the ones past the fourth finished their handshake and were then dropped
 * on the floor: the peer held a connection that was up and would never be
 * answered, which is a page that comes back missing a stylesheet.
 *
 * Past the backlog the peer is told. A reset is a connection a client can
 * retry; silence is one it waits out. */
#define BL_PORT 9010u
#define BL_N 6

static int32_t case_tcp_backlog(void) {
    int32_t ls, over;
    int32_t cl[BL_N];
    int32_t sv[BL_N];
    uint32_t i;
    int32_t st = 0;
    const uint8_t msg[] = { 'h', 'i' };

    for (i = 0; i < BL_N; i++) {
        cl[i] = -1;
        sv[i] = -1;
    }
    ls = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (ls < 0 || pm_metal_net_ip_bind(ls, LO4, BL_PORT) != 0
        || pm_metal_net_ip_listen(ls, BL_N) != 0) {
        return fail("backlog listen");
    }
    /* Everything a page load opens, before the server takes any of it. */
    for (i = 0; i < BL_N; i++) {
        cl[i] = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
        if (cl[i] < 0 || pm_metal_net_ip_connect(cl[i], LO4, (uint16_t)BL_PORT) != 1) {
            st = fail("backlog connect");
            goto out;
        }
    }
    for (i = 0; i < BL_N; i++) {
        sv[i] = pm_metal_net_ip_accept(ls);
        if (sv[i] < 0) {
            st = fail("a connection the listener took on was dropped");
            goto out;
        }
    }
    /* And they are live connections, not slots that merely counted. */
    if (pm_metal_net_ip_send(cl[BL_N - 1u], msg, sizeof(msg)) != (int32_t)sizeof(msg)) {
        st = fail("backlog send");
        goto out;
    }
    {
        uint8_t buf[8];
        uint32_t spin;
        int32_t n = 0;
        for (spin = 0; spin < 100u && n <= 0; spin++) {
            n = pm_metal_net_ip_recv(sv[BL_N - 1u], buf, sizeof(buf));
            pm_metal_net_ip_pump();
        }
        if (n != (int32_t)sizeof(msg) || buf[0] != 'h') {
            st = fail("the last one queued is a working connection");
            goto out;
        }
    }
    /* One past a full queue: the peer must learn, not park. Give the slots
     * back first — this table is shared with every other case. */
    for (i = 0; i < BL_N; i++) {
        (void)pm_metal_net_ip_close(sv[i]);
        (void)pm_metal_net_ip_close(cl[i]);
        sv[i] = -1;
        cl[i] = -1;
    }
    for (i = 0; i < BL_N; i++) {
        cl[i] = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
        if (cl[i] < 0 || pm_metal_net_ip_connect(cl[i], LO4, (uint16_t)BL_PORT) != 1) {
            st = fail("backlog refill");
            goto out;
        }
    }
    over = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (over < 0) {
        st = fail("backlog over socket");
        goto out;
    }
    (void)pm_metal_net_ip_connect(over, LO4, (uint16_t)BL_PORT);
    {
        uint32_t spin;
        int32_t sent = 0;
        for (spin = 0; spin < 100u; spin++) {
            pm_metal_net_ip_pump();
            sent = pm_metal_net_ip_send(over, msg, sizeof(msg));
            if (sent < 0) {
                break;
            }
        }
        if (sent >= 0) {
            st = fail("one past the backlog was left hanging instead of reset");
        }
    }
    (void)pm_metal_net_ip_close(over);
out:
    for (i = 0; i < BL_N; i++) {
        if (sv[i] >= 0) {
            (void)pm_metal_net_ip_close(sv[i]);
        }
        if (cl[i] >= 0) {
            (void)pm_metal_net_ip_close(cl[i]);
        }
    }
    (void)pm_metal_net_ip_close(ls);
    return st;
}

/* There is no table to run out of any more. What used to be thirty-two
 * sockets in .bss — a third of a megabyte a seat carried whether it served
 * anything or not — is now one socket's worth of arena per open socket, and
 * the seat gets it back on close. Forty-eight is past what this build ships
 * with, so the seat says so first: that is a sentence a running program can
 * write, where before it was a number somebody had to rebuild. */
#define GROW_N 48u
#define GROW_PORT 9100u

static int32_t case_socket_growth(void) {
    int32_t fd[GROW_N];
    pm_util_mem_arena_t *arena = pm_metal_coop_arena();
    size_t before;
    size_t during;
    size_t after;
    uint32_t i;
    int32_t st = 0;

    for (i = 0; i < GROW_N; i++) {
        fd[i] = -1;
    }
    if (pm_util_limits_set("net.ip.socket", GROW_N + 16u) != 0) {
        return fail("asking for more sockets");
    }
    before = pm_util_mem_arena_heap_used(arena);
    for (i = 0; i < GROW_N; i++) {
        fd[i] = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
        if (fd[i] < 0) {
            st = fail("the stack stopped opening sockets at a fixed count");
            goto out;
        }
        if (pm_metal_net_ip_bind(fd[i], LO4, (uint16_t)(GROW_PORT + i)) != 0) {
            st = fail("a socket past the old table does not work like the rest");
            goto out;
        }
    }
    during = pm_util_limits_used(pm_util_limits_find("net.ip.socket"));
    if (during < GROW_N) {
        st = fail("the listing does not show the sockets that are open");
    }
out:
    for (i = 0; i < GROW_N; i++) {
        if (fd[i] >= 0) {
            (void)pm_metal_net_ip_close(fd[i]);
            fd[i] = -1;
        }
    }
    if (st == 0) {
        /* Closed has to mean given back, not merely marked free. A leak would
         * show here: the arena hands TLSF a pool at a time, so a thousand
         * sockets' worth of never-returned memory forces it to grow one. */
        uint32_t round;
        for (round = 0; round < 1000u && st == 0; round++) {
            int32_t one = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
            if (one < 0) {
                st = fail("opening a socket stopped working partway through");
                break;
            }
            (void)pm_metal_net_ip_close(one);
        }
        after = pm_util_mem_arena_heap_used(arena);
        if (st == 0 && after != before) {
            st = fail("closed sockets did not go back to the arena");
        }
    }
    (void)pm_util_limits_reset("net.ip.socket");
    return st;
}

/* The knob is what says no, and a seat may move it while it runs. */
static int32_t case_socket_limit(void) {
    int32_t k = pm_util_limits_find("net.ip.socket");
    uint32_t live;
    int32_t a = -1;
    int32_t b = -1;
    int32_t c = -1;
    int32_t st = 0;

    if (k < 0 || pm_util_limits_default(k) != 32u) {
        return fail("net.ip.socket is not on the listing with the number it shipped with");
    }
    live = pm_util_limits_used(k);
    if (pm_util_limits_set("net.ip.socket", live + 2u) != 0) {
        return fail("set");
    }
    a = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    b = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    c = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    if (a < 0 || b < 0) {
        st = fail("the knob refused inside its own number");
    } else if (c >= 0) {
        st = fail("the knob was told two and gave three");
    } else if (pm_util_limits_used(k) != live + 2u) {
        st = fail("the listing does not show what is really open");
    }
    if (a >= 0) {
        (void)pm_metal_net_ip_close(a);
    }
    if (b >= 0) {
        (void)pm_metal_net_ip_close(b);
    }
    if (c >= 0) {
        (void)pm_metal_net_ip_close(c);
    }
    if (pm_util_limits_reset("net.ip.socket") != 0) {
        return fail("reset");
    }
    if (st == 0) {
        /* And past the old wall once it is put back. */
        int32_t d = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
        if (d < 0) {
            st = fail("putting the knob back did not let the seat open sockets again");
        } else {
            (void)pm_metal_net_ip_close(d);
        }
    }
    return st;
}

/* A listener's depth is its own, and a seat can raise the ceiling it is held
 * to. Twenty at once is past what this build ships with (sixteen), so this
 * only passes because the knob was moved first. */
#define DEEP_PORT 9200u
#define DEEP_N 20u

static int32_t case_backlog_knob(void) {
    int32_t ls;
    int32_t cl[DEEP_N];
    int32_t sv[DEEP_N];
    uint32_t i;
    int32_t st = 0;

    for (i = 0; i < DEEP_N; i++) {
        cl[i] = -1;
        sv[i] = -1;
    }
    /* Twenty connections is forty-one sockets with their listener, so both
     * knobs have to be moved — which is the whole exercise. */
    if (pm_util_limits_set("net.ip.backlog", 32u) != 0
        || pm_util_limits_set("net.ip.socket", 64u) != 0) {
        return fail("backlog knob");
    }
    ls = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
    if (ls < 0 || pm_metal_net_ip_bind(ls, LO4, DEEP_PORT) != 0
        || pm_metal_net_ip_listen(ls, (int32_t)DEEP_N + 4) != 0) {
        st = fail("deep listen");
        goto out;
    }
    for (i = 0; i < DEEP_N; i++) {
        cl[i] = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
        if (cl[i] < 0 || pm_metal_net_ip_connect(cl[i], LO4, (uint16_t)DEEP_PORT) != 1) {
            st = fail("deep connect");
            goto out;
        }
    }
    for (i = 0; i < DEEP_N; i++) {
        sv[i] = pm_metal_net_ip_accept(ls);
        if (sv[i] < 0) {
            st = fail("a listener told to hold twenty dropped one");
            goto out;
        }
    }
out:
    for (i = 0; i < DEEP_N; i++) {
        if (sv[i] >= 0) {
            (void)pm_metal_net_ip_close(sv[i]);
        }
        if (cl[i] >= 0) {
            (void)pm_metal_net_ip_close(cl[i]);
        }
    }
    if (ls >= 0) {
        (void)pm_metal_net_ip_close(ls);
    }
    (void)pm_util_limits_reset("net.ip.backlog");
    (void)pm_util_limits_reset("net.ip.socket");
    return st;
}

static int32_t case_offbox(void) {
    int32_t st;
    if (peer_up() != 0) {
        peer_down();
        return fail("peer up");
    }
    st = case_arp_then_send();
    if (st == 0) {
        st = case_nexthop();
    }
    if (st == 0) {
        st = case_tcp_offbox_csum();
    }
    if (st == 0) {
        st = case_ping_offbox();
    }
    if (st == 0) {
        st = case_csum_drop();
    }
    peer_down();
    return st;
}

int32_t pm_metal_net_ip_tests(void) {
    if (case_ping_lo() != 0) {
        return 1;
    }
    if (case_udp_lo_sync() != 0) {
        return 1;
    }
    if (case_udp_park() != 0) {
        return 1;
    }
    if (case_tcp_lo_echo() != 0) {
        return 1;
    }
    if (case_tcp_accept_park() != 0) {
        return 1;
    }
    if (case_tcp_backlog() != 0) {
        return 1;
    }
    if (case_socket_growth() != 0) {
        return 1;
    }
    if (case_socket_limit() != 0) {
        return 1;
    }
    if (case_backlog_knob() != 0) {
        return 1;
    }
    if (case_multicast_lo() != 0) {
        return 1;
    }
    if (case_offbox() != 0) {
        return 1;
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.ip, tests, pm_metal_net_ip_tests);
