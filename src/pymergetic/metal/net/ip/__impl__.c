/* pymergetic.metal.net.ip — IPv4 + ICMP + UDP + TCP + lo.
 * TCP rexmit on L2 (sim drop); lo delivers in the same xmit. Strong pump. */
#include "pymergetic/metal/net/ip/__exports__.h"

#include "pymergetic/metal/async.h"
#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/util/mem.h"

#include <string.h>

#define PM_METAL_IP_SOCK_MAX 32
#define PM_METAL_IP_RX_MAX 8192
#define PM_METAL_IP_PKT_MAX 8232
#define PM_METAL_IP_TCP_MSS (PM_METAL_IP_PKT_MAX - 40u)
#define PM_METAL_IP_LO_BE 0x7f000001u
#define PM_METAL_IP_ACCEPT_MAX 4
#define PM_METAL_IP_REXMIT_MAX 2048
#define PM_METAL_IP_RTO_US 50000ull
#define PM_METAL_IP_L2_MAX 32u
#define PM_METAL_IP_RT_MAX 16u
#define PM_METAL_IP_MASK24 0xffffff00u

#define SK_UDP 1u
#define SK_TCP 2u

#define TCP_LISTEN 1u
#define TCP_SYN_SENT 2u
#define TCP_SYN_RCVD 3u
#define TCP_ESTAB 4u
#define TCP_FIN_WAIT 5u
#define TCP_CLOSE_WAIT 6u

#define TCP_FIN 0x01u
#define TCP_SYN 0x02u
#define TCP_RST 0x04u
#define TCP_PSH 0x08u
#define TCP_ACK 0x10u

struct pm_metal_sock {
    uint32_t used;
    uint8_t kind;
    uint8_t tcp_st;
    uint32_t bound;
    int32_t l2_h;
    uint32_t laddr_be;
    uint16_t lport;
    uint32_t raddr_be;
    uint16_t rport;
    uint32_t snd_nxt;
    uint32_t snd_una;
    uint32_t rcv_nxt;
    uint32_t iss;
    uint8_t rexmit[PM_METAL_IP_REXMIT_MAX];
    uint32_t rexmit_len;
    uint32_t rexmit_seq;
    uint8_t rexmit_flags;
    uint64_t rexmit_at;
    uint8_t rx[PM_METAL_IP_RX_MAX];
    uint32_t rx_len;
    uint32_t rx_addr_be;
    uint16_t rx_port;
    uint32_t peer_fin;
    pm_metal_async_task_t *waiter;
    int32_t listen_fd;
    int32_t accept_q[PM_METAL_IP_ACCEPT_MAX];
    uint32_t accept_n;
};

static pm_util_mem_arena_t *s_arena;
static uint32_t s_lo_up;
static uint32_t s_lo_addr_be = PM_METAL_IP_LO_BE;
static struct pm_metal_sock s_sk[PM_METAL_IP_SOCK_MAX];
struct pm_metal_ip_l2 {
    int32_t h;
    uint32_t addr_be;
    uint32_t mask_be;
};

struct pm_metal_ip_rt {
    uint32_t used;
    uint32_t dst_be;
    uint32_t mask_be;
    int32_t h;
};

static struct pm_metal_ip_l2 s_l2[PM_METAL_IP_L2_MAX];
static uint32_t s_l2_n;
static int32_t s_l2_cur = -1;
static uint32_t s_if_pending_be;
static uint32_t s_if_pending_mask;
static struct pm_metal_ip_rt s_rt[PM_METAL_IP_RT_MAX];
static int32_t s_rx_l2 = -1;
static uint8_t s_ping_out[PM_METAL_IP_RX_MAX];
static uint32_t s_ping_len;
static uint16_t s_eph = 49152u;

static uint32_t mask_bits(uint32_t m) {
    uint32_t n = 0;
    while (m != 0) {
        n += m & 1u;
        m >>= 1;
    }
    return n;
}

static void rt_clear(void) {
    memset(s_rt, 0, sizeof(s_rt));
}

static void rt_del_h(int32_t h) {
    uint32_t i;
    for (i = 0; i < PM_METAL_IP_RT_MAX; i++) {
        if (s_rt[i].used && s_rt[i].h == h) {
            s_rt[i].used = 0;
        }
    }
}

static void rt_upsert(uint32_t dst_be, uint32_t mask_be, int32_t h) {
    uint32_t i;
    int32_t slot = -1;
    for (i = 0; i < PM_METAL_IP_RT_MAX; i++) {
        if (s_rt[i].used && s_rt[i].dst_be == dst_be && s_rt[i].mask_be == mask_be) {
            s_rt[i].h = h;
            return;
        }
        if (!s_rt[i].used && slot < 0) {
            slot = (int32_t)i;
        }
    }
    if (slot < 0) {
        return;
    }
    s_rt[slot].used = 1;
    s_rt[slot].dst_be = dst_be;
    s_rt[slot].mask_be = mask_be;
    s_rt[slot].h = h;
}

static int32_t rt_lookup(uint32_t dst_be) {
    uint32_t i;
    uint32_t best = 0;
    int32_t h = -1;
    int32_t def = -1;
    for (i = 0; i < PM_METAL_IP_RT_MAX; i++) {
        uint32_t bits;
        if (!s_rt[i].used) {
            continue;
        }
        if (s_rt[i].mask_be == 0) {
            def = s_rt[i].h;
            continue;
        }
        if ((dst_be & s_rt[i].mask_be) != (s_rt[i].dst_be & s_rt[i].mask_be)) {
            continue;
        }
        bits = mask_bits(s_rt[i].mask_be);
        if (h < 0 || bits >= best) {
            best = bits;
            h = s_rt[i].h;
        }
    }
    return h >= 0 ? h : def;
}

static void l2_clear(void) {
    memset(s_l2, 0, sizeof(s_l2));
    s_l2_n = 0;
    s_l2_cur = -1;
    s_if_pending_be = 0;
    s_if_pending_mask = 0;
    rt_clear();
}

static uint32_t l2_addr_of(int32_t h) {
    uint32_t i;
    for (i = 0; i < s_l2_n; i++) {
        if (s_l2[i].h == h) {
            return s_l2[i].addr_be;
        }
    }
    return 0;
}

static int32_t l2_has(int32_t h) {
    uint32_t i;
    for (i = 0; i < s_l2_n; i++) {
        if (s_l2[i].h == h) {
            return 1;
        }
    }
    return 0;
}

static int32_t l2_h_for_addr(uint32_t addr_be) {
    uint32_t i;
    if (addr_be == 0) {
        return -1;
    }
    for (i = 0; i < s_l2_n; i++) {
        if (s_l2[i].addr_be == addr_be) {
            return s_l2[i].h;
        }
    }
    return -1;
}

static void l2_apply_pending(void);

static int32_t if_up_mask(int32_t h, uint32_t addr_be, uint32_t mask_be) {
    uint32_t i;
    if (s_arena == NULL || addr_be == 0 || mask_be == 0 || !l2_has(h)) {
        return -1;
    }
    for (i = 0; i < s_l2_n; i++) {
        if (s_l2[i].h == h) {
            s_l2[i].addr_be = addr_be;
            s_l2[i].mask_be = mask_be;
            break;
        }
    }
    rt_upsert(addr_be & mask_be, mask_be, h);
    if (s_l2_cur < 0 || s_l2_cur == h) {
        s_l2_cur = h;
        rt_upsert(0, 0, h);
    }
    return 0;
}

static void l2_apply_pending(void) {
    if (s_if_pending_be == 0 || s_l2_cur < 0) {
        return;
    }
    (void)if_up_mask(s_l2_cur, s_if_pending_be,
        s_if_pending_mask != 0 ? s_if_pending_mask : PM_METAL_IP_MASK24);
    s_if_pending_be = 0;
    s_if_pending_mask = 0;
}

static uint32_t ip_src_route(uint32_t dst) {
    int32_t h;
    uint32_t i;
    uint32_t addr;
    if (dst == s_lo_addr_be || dst == 0x7f000001u) {
        return s_lo_addr_be;
    }
    h = rt_lookup(dst);
    addr = l2_addr_of(h);
    if (addr != 0) {
        return addr;
    }
    for (i = 0; i < s_l2_n; i++) {
        if (s_l2[i].addr_be != 0) {
            return s_l2[i].addr_be;
        }
    }
    return s_lo_addr_be;
}

static uint32_t ip_src_for(const struct pm_metal_sock *s, uint32_t dst) {
    uint32_t addr;
    if (s != NULL && s->laddr_be != 0 && s->laddr_be != 0xffffffffu) {
        return s->laddr_be;
    }
    if (s != NULL && s->l2_h >= 0) {
        addr = l2_addr_of(s->l2_h);
        if (addr != 0) {
            return addr;
        }
    }
    return ip_src_route(dst);
}

static int32_t l2_tx_for_pkt(uint32_t src_be, uint32_t dst_be) {
    int32_t h;
    h = l2_h_for_addr(src_be);
    if (h >= 0) {
        return h;
    }
    h = rt_lookup(dst_be);
    if (h >= 0) {
        return h;
    }
    return s_l2_n != 0 ? s_l2[0].h : -1;
}

static int32_t l2_addr_ours(uint32_t dst_be) {
    uint32_t i;
    if (dst_be == 0) {
        return 0;
    }
    for (i = 0; i < s_l2_n; i++) {
        if (s_l2[i].addr_be == dst_be) {
            return 1;
        }
    }
    return (s_if_pending_be != 0 && dst_be == s_if_pending_be) ? 1 : 0;
}

static uint16_t ip_csum(const uint8_t *p, uint32_t n) {
    uint32_t s = 0;
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

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void write_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void write_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void sock_wake(struct pm_metal_sock *s) {
    if (s->waiter != NULL) {
        (void)pm_metal_async_post_task(s->waiter);
        s->waiter = NULL;
    }
}

static int32_t sock_alloc(uint8_t kind) {
    uint32_t i;
    for (i = 0; i < PM_METAL_IP_SOCK_MAX; i++) {
        if (!s_sk[i].used) {
            memset(&s_sk[i], 0, sizeof(s_sk[i]));
            s_sk[i].used = 1;
            s_sk[i].kind = kind;
            s_sk[i].listen_fd = -1;
            s_sk[i].l2_h = -1;
            return (int32_t)i;
        }
    }
    return -1;
}

static struct pm_metal_sock *sock_get(int32_t fd, uint8_t kind) {
    if (fd < 0 || (uint32_t)fd >= PM_METAL_IP_SOCK_MAX || !s_sk[fd].used) {
        return NULL;
    }
    if (kind != 0 && s_sk[fd].kind != kind) {
        return NULL;
    }
    return &s_sk[fd];
}

static uint16_t eph_port(void) {
    uint16_t p = s_eph++;
    if (s_eph < 49152u) {
        s_eph = 49152u;
    }
    return p;
}

static void ip_input(const uint8_t *pkt, uint32_t len);

int32_t pm_metal_net_ip_rx_from(int32_t h, const uint8_t *frame, uint16_t len) {
    const uint8_t *pkt = frame;
    uint32_t n = len;
    int32_t prev = s_rx_l2;
    if (frame == NULL || len == 0) {
        return -1;
    }
    s_rx_l2 = h;
    if (len >= 14u && ((uint16_t)((frame[12] << 8) | frame[13]) == 0x0800u)) {
        pkt = frame + 14;
        n = (uint32_t)len - 14u;
    }
    ip_input(pkt, n);
    s_rx_l2 = prev;
    return 0;
}

int32_t pm_metal_net_ip_rx(const uint8_t *frame, uint16_t len) {
    return pm_metal_net_ip_rx_from(-1, frame, len);
}

static void ip_output(const uint8_t *pkt, uint32_t len) {
    if (s_lo_up && len >= 20u) {
        uint32_t dst = read_be32(pkt + 16);
        if (dst == s_lo_addr_be || dst == 0x7f000001u) {
            ip_input(pkt, len);
            return;
        }
    }
    if (len >= 20u) {
        int32_t h = l2_tx_for_pkt(read_be32(pkt + 12), read_be32(pkt + 16));
        if (h >= 0) {
            uint8_t frame[PM_METAL_IP_PKT_MAX + 14u];
            uint8_t srcmac[6];
            uint32_t flen;
            if (len > PM_METAL_IP_PKT_MAX) {
                return;
            }
            memset(frame, 0xff, 6);
            memset(srcmac, 0, 6);
            pm_metal_drivers_net_mac(h, srcmac);
            memcpy(frame + 6, srcmac, 6);
            frame[12] = 0x08;
            frame[13] = 0x00;
            memcpy(frame + 14, pkt, len);
            flen = len + 14u;
            (void)pm_metal_drivers_net_tx(h, frame, (uint16_t)flen);
        }
    }
}

static void tcp_emit(struct pm_metal_sock *s, uint8_t flags, const uint8_t *data, uint32_t dlen,
    uint32_t seq) {
    uint32_t th = 20u;
    uint32_t total = 20u + th + dlen;
    uint8_t pkt[PM_METAL_IP_PKT_MAX];
    if (total > PM_METAL_IP_PKT_MAX) {
        return;
    }
    memset(pkt, 0, total);
    pkt[0] = 0x45;
    write_be16(pkt + 2, (uint16_t)total);
    pkt[8] = 64;
    pkt[9] = 6;
    write_be32(pkt + 12, ip_src_for(s, s->raddr_be ? s->raddr_be : s_lo_addr_be));
    write_be32(pkt + 16, s->raddr_be ? s->raddr_be : s_lo_addr_be);
    uint16_t cs = ip_csum(pkt, 20);
    write_be16(pkt + 10, cs);
    write_be16(pkt + 20, s->lport);
    write_be16(pkt + 22, s->rport);
    write_be32(pkt + 24, seq);
    write_be32(pkt + 28, s->rcv_nxt);
    pkt[32] = 0x50;
    pkt[33] = flags;
    write_be16(pkt + 34, 4096);
    if (dlen != 0 && data != NULL) {
        memcpy(pkt + 40, data, dlen);
    }
    ip_output(pkt, total);
}

static void tcp_xmit(struct pm_metal_sock *s, uint8_t flags, const uint8_t *data, uint32_t dlen) {
    uint32_t seq = s->snd_nxt;
    if ((flags & TCP_SYN) != 0 || (flags & TCP_FIN) != 0) {
        s->snd_nxt += 1u;
    }
    s->snd_nxt += dlen;
    tcp_emit(s, flags, data, dlen, seq);
    if (dlen != 0 && dlen <= PM_METAL_IP_REXMIT_MAX && s->snd_una < s->snd_nxt) {
        memcpy(s->rexmit, data, dlen);
        s->rexmit_len = dlen;
        s->rexmit_seq = seq;
        s->rexmit_flags = flags;
        s->rexmit_at = pm_metal_async_mono_us() + PM_METAL_IP_RTO_US;
    }
}

static void tcp_check_timeouts(void) {
    uint64_t now = pm_metal_async_mono_us();
    uint32_t i;
    for (i = 0; i < PM_METAL_IP_SOCK_MAX; i++) {
        struct pm_metal_sock *s = &s_sk[i];
        if (!s->used || s->kind != SK_TCP || s->rexmit_len == 0) {
            continue;
        }
        if (now < s->rexmit_at) {
            continue;
        }
        tcp_emit(s, s->rexmit_flags, s->rexmit, s->rexmit_len, s->rexmit_seq);
        s->rexmit_at = now + PM_METAL_IP_RTO_US;
    }
}

static void tcp_queue_accept(struct pm_metal_sock *ls, int32_t child) {
    if (ls->accept_n < PM_METAL_IP_ACCEPT_MAX) {
        ls->accept_q[ls->accept_n++] = child;
        sock_wake(ls);
    }
}

static struct pm_metal_sock *tcp_find(uint32_t src, uint16_t sport, uint32_t dst, uint16_t dport) {
    uint32_t i;
    struct pm_metal_sock *listen = NULL;
    for (i = 0; i < PM_METAL_IP_SOCK_MAX; i++) {
        struct pm_metal_sock *s = &s_sk[i];
        if (!s->used || s->kind != SK_TCP) {
            continue;
        }
        if (s->tcp_st == TCP_LISTEN && s->lport == dport) {
            if ((s->laddr_be == 0 || s->laddr_be == 0xffffffffu || s->laddr_be == dst)
                && (s->l2_h < 0 || s_rx_l2 < 0 || s->l2_h == s_rx_l2)) {
                listen = s;
            }
            continue;
        }
        if (s->lport == dport && s->rport == sport && s->raddr_be == src) {
            if (s->l2_h >= 0 && s_rx_l2 >= 0 && s->l2_h != s_rx_l2) {
                continue;
            }
            return s;
        }
    }
    return listen;
}

static void tcp_input(uint32_t src, uint32_t dst, const uint8_t *th, uint32_t thlen) {
    if (thlen < 20u) {
        return;
    }
    uint16_t sport = read_be16(th);
    uint16_t dport = read_be16(th + 2);
    uint32_t seq = read_be32(th + 4);
    uint32_t ack = read_be32(th + 8);
    uint8_t off = (uint8_t)((th[12] >> 4) * 4u);
    uint8_t flags = th[13];
    const uint8_t *data = th + off;
    uint32_t dlen = thlen > off ? thlen - off : 0;
    struct pm_metal_sock *s = tcp_find(src, sport, dst, dport);
    if (s == NULL) {
        return;
    }
    if ((flags & TCP_ACK) != 0 && ack > s->snd_una && ack <= s->snd_nxt) {
        s->snd_una = ack;
        if (s->rexmit_len != 0 && s->snd_una >= s->rexmit_seq + s->rexmit_len) {
            s->rexmit_len = 0;
        }
    }
    if (s->tcp_st == TCP_LISTEN && (flags & TCP_SYN) != 0 && (flags & TCP_ACK) == 0) {
        int32_t cfd = sock_alloc(SK_TCP);
        if (cfd < 0) {
            return;
        }
        struct pm_metal_sock *c = &s_sk[cfd];
        c->bound = 1;
        c->l2_h = s->l2_h;
        c->laddr_be = s->laddr_be;
        c->lport = s->lport;
        c->raddr_be = src;
        c->rport = sport;
        c->iss = 1000u;
        c->snd_nxt = c->iss;
        c->snd_una = c->iss;
        c->rcv_nxt = seq + 1u;
        c->tcp_st = TCP_SYN_RCVD;
        c->listen_fd = (int32_t)(s - s_sk);
        tcp_xmit(c, (uint8_t)(TCP_SYN | TCP_ACK), NULL, 0);
        return;
    }
    if (s->tcp_st == TCP_SYN_SENT && (flags & TCP_SYN) != 0 && (flags & TCP_ACK) != 0) {
        s->rcv_nxt = seq + 1u;
        s->tcp_st = TCP_ESTAB;
        s->snd_una = s->snd_nxt;
        tcp_xmit(s, TCP_ACK, NULL, 0);
        sock_wake(s);
        return;
    }
    if (s->tcp_st == TCP_SYN_RCVD && (flags & TCP_ACK) != 0) {
        s->tcp_st = TCP_ESTAB;
        s->snd_una = s->snd_nxt;
        if (s->listen_fd >= 0) {
            tcp_queue_accept(&s_sk[s->listen_fd], (int32_t)(s - s_sk));
        }
        return;
    }
    if (s->tcp_st == TCP_ESTAB || s->tcp_st == TCP_FIN_WAIT || s->tcp_st == TCP_CLOSE_WAIT) {
        if (dlen != 0) {
            if (seq != s->rcv_nxt) {
                tcp_xmit(s, TCP_ACK, NULL, 0);
                return;
            }
            uint32_t room = PM_METAL_IP_RX_MAX - s->rx_len;
            uint32_t n = dlen < room ? dlen : room;
            memcpy(s->rx + s->rx_len, data, n);
            s->rx_len += n;
            s->rcv_nxt += n;
            tcp_xmit(s, TCP_ACK, NULL, 0);
            sock_wake(s);
        }
        if ((flags & TCP_FIN) != 0) {
            s->rcv_nxt += 1u;
            s->peer_fin = 1;
            if (s->tcp_st == TCP_ESTAB) {
                s->tcp_st = TCP_CLOSE_WAIT;
            }
            tcp_xmit(s, TCP_ACK, NULL, 0);
            sock_wake(s);
        }
    }
}

static void ip_input(const uint8_t *pkt, uint32_t len) {
    if (len < 20u || (pkt[0] >> 4) != 4) {
        return;
    }
    uint32_t ihl = (uint32_t)(pkt[0] & 0x0fu) * 4u;
    if (ihl < 20u || len < ihl) {
        return;
    }
    uint32_t dst = read_be32(pkt + 16);
    uint32_t src = read_be32(pkt + 12);
    uint32_t ours = 0;
    if (s_lo_up && (dst == s_lo_addr_be || dst == 0x7f000001u)) {
        ours = 1;
    }
    if (l2_addr_ours(dst)) {
        ours = 1;
    }
    if (dst == 0xffffffffu) {
        ours = 1;
    }
    if (!ours) {
        return;
    }
    uint8_t proto = pkt[9];
    const uint8_t *l4 = pkt + ihl;
    uint32_t l4len = len - ihl;
    if (proto == 1 && l4len >= 8u && l4[0] == 8) {
        uint8_t reply[PM_METAL_IP_PKT_MAX];
        if (len > PM_METAL_IP_PKT_MAX) {
            return;
        }
        memcpy(reply, pkt, len);
        write_be32(reply + 12, dst);
        write_be32(reply + 16, src);
        reply[10] = 0;
        reply[11] = 0;
        uint16_t cs = ip_csum(reply, ihl);
        write_be16(reply + 10, cs);
        reply[ihl] = 0;
        reply[ihl + 2] = 0;
        reply[ihl + 3] = 0;
        cs = ip_csum(reply + ihl, l4len);
        write_be16(reply + ihl + 2, cs);
        uint32_t copy = l4len > PM_METAL_IP_RX_MAX ? PM_METAL_IP_RX_MAX : l4len;
        memcpy(s_ping_out, reply + ihl, copy);
        s_ping_len = copy;
        return;
    }
    if (proto == 6) {
        tcp_input(src, dst, l4, l4len);
        return;
    }
    if (proto == 17 && l4len >= 8u) {
        uint16_t dport = read_be16(l4 + 2);
        uint16_t sport = read_be16(l4);
        const uint8_t *payload = l4 + 8;
        uint32_t plen = l4len - 8u;
        uint32_t i;
        for (i = 0; i < PM_METAL_IP_SOCK_MAX; i++) {
            struct pm_metal_sock *pcb = &s_sk[i];
            if (!pcb->used || pcb->kind != SK_UDP || !pcb->bound) {
                continue;
            }
            if (pcb->lport != dport) {
                continue;
            }
            if (pcb->l2_h >= 0 && s_rx_l2 >= 0 && pcb->l2_h != s_rx_l2) {
                continue;
            }
            if (pcb->laddr_be != 0 && pcb->laddr_be != 0xffffffffu && pcb->laddr_be != dst) {
                continue;
            }
            if (plen > PM_METAL_IP_RX_MAX) {
                plen = PM_METAL_IP_RX_MAX;
            }
            memcpy(pcb->rx, payload, plen);
            pcb->rx_len = plen;
            pcb->rx_addr_be = src;
            pcb->rx_port = sport;
            sock_wake(pcb);
            return;
        }
    }
}

int32_t pm_metal_net_ip_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_sk, 0, sizeof(s_sk));
    l2_clear();
    s_lo_up = 0;
    s_rx_l2 = -1;
    s_ping_len = 0;
    s_eph = 49152u;
    (void)s_arena;
    return 0;
}

void pm_metal_net_ip_deinit(void) {
    memset(s_sk, 0, sizeof(s_sk));
    s_lo_up = 0;
    l2_clear();
    s_rx_l2 = -1;
    s_arena = NULL;
}

int32_t pm_metal_net_ip_lo_up(void) {
    if (s_arena == NULL) {
        return -1;
    }
    s_lo_up = 1;
    return 0;
}

static int32_t first_netdev(void) {
    int32_t i;
    for (i = 0; i < 32; i++) {
        if (pm_metal_drivers_net_dt_id(i) >= 0) {
            return i;
        }
    }
    return -1;
}

int32_t pm_metal_net_ip_if_up_mask(int32_t h, uint32_t addr_be, uint32_t mask_be) {
    if (pm_metal_net_ip_l2_attach(h) != 0) {
        return -1;
    }
    return if_up_mask(h, addr_be, mask_be);
}

int32_t pm_metal_net_ip_if_up_h(int32_t h, uint32_t addr_be) {
    return pm_metal_net_ip_if_up_mask(h, addr_be, PM_METAL_IP_MASK24);
}

int32_t pm_metal_net_ip_if_up(uint32_t addr_be) {
    int32_t h;
    if (s_arena == NULL || addr_be == 0) {
        return -1;
    }
    if (s_l2_cur >= 0) {
        return if_up_mask(s_l2_cur, addr_be, PM_METAL_IP_MASK24);
    }
    if (s_l2_n != 0) {
        return if_up_mask(s_l2[0].h, addr_be, PM_METAL_IP_MASK24);
    }
    h = first_netdev();
    if (h >= 0) {
        return pm_metal_net_ip_if_up_mask(h, addr_be, PM_METAL_IP_MASK24);
    }
    s_if_pending_be = addr_be;
    s_if_pending_mask = PM_METAL_IP_MASK24;
    return 0;
}

uint32_t pm_metal_net_ip_if_addr(int32_t h) {
    return l2_addr_of(h);
}

void pm_metal_net_ip_pump(void) {
    uint32_t i;
    tcp_check_timeouts();
    for (i = 0; i < s_l2_n; i++) {
        (void)pm_metal_drivers_net_poll(s_l2[i].h);
    }
}

int32_t pm_metal_net_ip_l2_attach(int32_t h) {
    uint32_t i;
    if (pm_metal_drivers_net_dt_id(h) < 0) {
        return -1;
    }
    for (i = 0; i < s_l2_n; i++) {
        if (s_l2[i].h == h) {
            s_l2_cur = h;
            l2_apply_pending();
            return 0;
        }
    }
    if (s_l2_n >= PM_METAL_IP_L2_MAX) {
        return -1;
    }
    s_l2[s_l2_n].h = h;
    s_l2[s_l2_n].addr_be = 0;
    s_l2[s_l2_n].mask_be = 0;
    s_l2_n++;
    s_l2_cur = h;
    l2_apply_pending();
    return 0;
}

int32_t pm_metal_net_ip_l2_detach(int32_t h) {
    uint32_t i;
    uint32_t j;
    int32_t found = 0;
    for (i = 0; i < s_l2_n; i++) {
        if (s_l2[i].h == h) {
            for (j = i; j + 1u < s_l2_n; j++) {
                s_l2[j] = s_l2[j + 1u];
            }
            s_l2_n--;
            found = 1;
            break;
        }
    }
    if (!found) {
        return -1;
    }
    rt_del_h(h);
    if (s_l2_cur == h) {
        s_l2_cur = s_l2_n != 0 ? s_l2[s_l2_n - 1u].h : -1;
        if (s_l2_cur >= 0) {
            rt_upsert(0, 0, s_l2_cur);
        }
    }
    return 0;
}

int32_t pm_metal_net_l2_attach(const char *name, const pm_metal_net_l2_ops_t *ops) {
    int32_t dt;
    int32_t h;
    if (name == NULL || name[0] == 0 || ops == NULL) {
        return -1;
    }
    dt = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, name, PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, 0);
    if (dt < 0) {
        return -1;
    }
    h = pm_metal_drivers_net_bind(dt, ops);
    if (h < 0) {
        return h;
    }
    return pm_metal_net_ip_l2_attach(h);
}

int32_t pm_metal_net_ip_socket(int32_t type) {
    if (type == PM_METAL_NET_IP_SOCK_STREAM) {
        return sock_alloc(SK_TCP);
    }
    if (type == PM_METAL_NET_IP_SOCK_DGRAM) {
        return sock_alloc(SK_UDP);
    }
    return -1;
}

int32_t pm_metal_net_ip_close(int32_t fd) {
    struct pm_metal_sock *s = sock_get(fd, 0);
    if (s == NULL) {
        return -1;
    }
    if (s->kind == SK_TCP && s->tcp_st == TCP_ESTAB) {
        tcp_xmit(s, (uint8_t)(TCP_FIN | TCP_ACK), NULL, 0);
        s->tcp_st = TCP_FIN_WAIT;
    }
    memset(s, 0, sizeof(*s));
    return 0;
}

int32_t pm_metal_net_ip_bind(int32_t fd, uint32_t addr_be, uint16_t port_host) {
    struct pm_metal_sock *s = sock_get(fd, 0);
    int32_t h;
    if (s == NULL) {
        return -1;
    }
    s->laddr_be = addr_be;
    s->lport = port_host;
    s->bound = 1;
    h = l2_h_for_addr(addr_be);
    if (h >= 0) {
        s->l2_h = h;
    }
    return 0;
}

int32_t pm_metal_net_ip_bind_l2(int32_t fd, int32_t h) {
    struct pm_metal_sock *s = sock_get(fd, 0);
    if (s == NULL || !l2_has(h)) {
        return -1;
    }
    s->l2_h = h;
    return 0;
}

int32_t pm_metal_net_ip_listen(int32_t fd, int32_t backlog) {
    struct pm_metal_sock *s = sock_get(fd, SK_TCP);
    (void)backlog;
    if (s == NULL || !s->bound) {
        return -1;
    }
    s->tcp_st = TCP_LISTEN;
    return 0;
}

int32_t pm_metal_net_ip_accept(int32_t fd) {
    struct pm_metal_sock *s = sock_get(fd, SK_TCP);
    if (s == NULL || s->tcp_st != TCP_LISTEN) {
        return -1;
    }
    if (s->accept_n == 0) {
        pm_metal_async_task_t *cur = pm_metal_async_current_task();
        if (cur != NULL) {
            s->waiter = cur;
            return -2;
        }
        return -1;
    }
    int32_t child = s->accept_q[0];
    uint32_t i;
    for (i = 1; i < s->accept_n; i++) {
        s->accept_q[i - 1u] = s->accept_q[i];
    }
    s->accept_n--;
    return child;
}

int32_t pm_metal_net_ip_connect(int32_t fd, uint32_t addr_be, uint16_t port_host) {
    struct pm_metal_sock *s = sock_get(fd, SK_TCP);
    if (s == NULL) {
        return -1;
    }
    if (!s->bound) {
        s->laddr_be = ip_src_for(s, addr_be);
        s->lport = eph_port();
        s->bound = 1;
    } else if (s->laddr_be == 0 || s->laddr_be == 0xffffffffu) {
        s->laddr_be = ip_src_for(s, addr_be);
    }
    s->raddr_be = addr_be;
    s->rport = port_host;
    s->iss = 2000u;
    s->snd_nxt = s->iss;
    s->snd_una = s->iss;
    s->tcp_st = TCP_SYN_SENT;
    tcp_xmit(s, TCP_SYN, NULL, 0);
    if (s->tcp_st == TCP_ESTAB) {
        return 1;
    }
    pm_metal_async_task_t *cur = pm_metal_async_current_task();
    if (cur != NULL) {
        s->waiter = cur;
        return 0;
    }
    return 0;
}

int32_t pm_metal_net_ip_send(int32_t fd, const uint8_t *buf, uint32_t len) {
    struct pm_metal_sock *s = sock_get(fd, SK_TCP);
    if (s == NULL || buf == NULL || s->tcp_st != TCP_ESTAB) {
        return -1;
    }
    if (len > PM_METAL_IP_TCP_MSS) {
        len = PM_METAL_IP_TCP_MSS;
    }
    tcp_xmit(s, (uint8_t)(TCP_PSH | TCP_ACK), buf, len);
    return (int32_t)len;
}

int32_t pm_metal_net_ip_recv(int32_t fd, uint8_t *buf, uint32_t len) {
    struct pm_metal_sock *s = sock_get(fd, SK_TCP);
    if (s == NULL || buf == NULL) {
        return -1;
    }
    if (s->rx_len == 0) {
        if (s->peer_fin) {
            return -2;
        }
        pm_metal_async_task_t *cur = pm_metal_async_current_task();
        if (cur != NULL) {
            s->waiter = cur;
            return 0;
        }
        return -1;
    }
    uint32_t n = s->rx_len < len ? s->rx_len : len;
    memcpy(buf, s->rx, n);
    if (n < s->rx_len) {
        memmove(s->rx, s->rx + n, s->rx_len - n);
    }
    s->rx_len -= n;
    return (int32_t)n;
}

int32_t pm_metal_net_ip_sendto(int32_t fd, const uint8_t *buf, uint32_t len, uint32_t addr_be,
    uint16_t port_host) {
    struct pm_metal_sock *pcb = sock_get(fd, SK_UDP);
    if (pcb == NULL || buf == NULL) {
        return -1;
    }
    if (!pcb->bound || len > PM_METAL_IP_RX_MAX) {
        return -1;
    }
    uint32_t total = 20u + 8u + len;
    uint8_t pkt[PM_METAL_IP_PKT_MAX];
    if (total > PM_METAL_IP_PKT_MAX) {
        return -1;
    }
    memset(pkt, 0, total);
    pkt[0] = 0x45;
    write_be16(pkt + 2, (uint16_t)total);
    pkt[8] = 64;
    pkt[9] = 17;
    write_be32(pkt + 12, ip_src_for(pcb, addr_be));
    write_be32(pkt + 16, addr_be);
    uint16_t cs = ip_csum(pkt, 20);
    write_be16(pkt + 10, cs);
    write_be16(pkt + 20, pcb->lport);
    write_be16(pkt + 22, port_host);
    write_be16(pkt + 24, (uint16_t)(8u + len));
    memcpy(pkt + 28, buf, len);
    ip_output(pkt, total);
    return (int32_t)len;
}

int32_t pm_metal_net_ip_recvfrom(int32_t fd, uint8_t *buf, uint32_t len, uint32_t *addr_be,
    uint16_t *port_host) {
    struct pm_metal_sock *pcb = sock_get(fd, SK_UDP);
    if (pcb == NULL || buf == NULL) {
        return -1;
    }
    if (pcb->rx_len == 0) {
        pm_metal_async_task_t *cur = pm_metal_async_current_task();
        if (cur != NULL) {
            pcb->waiter = cur;
            return 0;
        }
        return -1;
    }
    uint32_t n = pcb->rx_len < len ? pcb->rx_len : len;
    memcpy(buf, pcb->rx, n);
    if (addr_be != NULL) {
        *addr_be = pcb->rx_addr_be;
    }
    if (port_host != NULL) {
        *port_host = pcb->rx_port;
    }
    pcb->rx_len = 0;
    return (int32_t)n;
}

int32_t pm_metal_net_ip_ping4(uint32_t addr_be, const uint8_t *payload, uint32_t plen, uint8_t *out,
    uint32_t *out_len) {
    if (!s_lo_up || payload == NULL || out == NULL || out_len == NULL) {
        return -1;
    }
    uint32_t icmp_len = 8u + plen;
    uint32_t total = 20u + icmp_len;
    uint8_t pkt[PM_METAL_IP_PKT_MAX];
    if (total > PM_METAL_IP_PKT_MAX) {
        return -1;
    }
    memset(pkt, 0, total);
    pkt[0] = 0x45;
    write_be16(pkt + 2, (uint16_t)total);
    pkt[8] = 64;
    pkt[9] = 1;
    write_be32(pkt + 12, s_lo_addr_be);
    write_be32(pkt + 16, addr_be);
    uint16_t cs = ip_csum(pkt, 20);
    write_be16(pkt + 10, cs);
    pkt[20] = 8;
    memcpy(pkt + 28, payload, plen);
    cs = ip_csum(pkt + 20, icmp_len);
    write_be16(pkt + 22, cs);
    s_ping_len = 0;
    ip_output(pkt, total);
    if (s_ping_len == 0) {
        return -1;
    }
    uint32_t n = s_ping_len < *out_len ? s_ping_len : *out_len;
    memcpy(out, s_ping_out, n);
    *out_len = n;
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_init, pm_metal_net_ip_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_deinit, pm_metal_net_ip_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_lo_up, pm_metal_net_ip_lo_up, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_if_up, pm_metal_net_ip_if_up, int32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_if_up_h, pm_metal_net_ip_if_up_h, int32_t(int32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_if_up_mask, pm_metal_net_ip_if_up_mask, int32_t(int32_t, uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_if_addr, pm_metal_net_ip_if_addr, uint32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_pump, pm_metal_net_ip_pump, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_l2_attach, pm_metal_net_ip_l2_attach, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_l2_detach, pm_metal_net_ip_l2_detach, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_l2_attach, pm_metal_net_l2_attach, int32_t(const char *, const pm_metal_net_l2_ops_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_rx, pm_metal_net_ip_rx, int32_t(const uint8_t *, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_rx_from, pm_metal_net_ip_rx_from, int32_t(int32_t, const uint8_t *, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_socket, pm_metal_net_ip_socket, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_close, pm_metal_net_ip_close, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_bind, pm_metal_net_ip_bind, int32_t(int32_t, uint32_t, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_bind_l2, pm_metal_net_ip_bind_l2, int32_t(int32_t, int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_listen, pm_metal_net_ip_listen, int32_t(int32_t, int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_connect, pm_metal_net_ip_connect, int32_t(int32_t, uint32_t, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_send, pm_metal_net_ip_send, int32_t(int32_t, const uint8_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_recv, pm_metal_net_ip_recv, int32_t(int32_t, uint8_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_accept, pm_metal_net_ip_accept, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_sendto, pm_metal_net_ip_sendto, int32_t(int32_t, const uint8_t *, uint32_t, uint32_t, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_recvfrom, pm_metal_net_ip_recvfrom, int32_t(int32_t, uint8_t *, uint32_t, uint32_t *, uint16_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_ping4, pm_metal_net_ip_ping4, int32_t(uint32_t, const uint8_t *, uint32_t, uint8_t *, uint32_t *));

PM_MOD_BOOT_READY_C(pymergetic.metal.net.ip, pm_metal_net_ip_init, pm_metal_net_ip_deinit, pm_metal_net_ip_lo_up);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.ip, pymergetic.metal.async);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.ip, pymergetic.metal.drivers.net);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.ip, pymergetic.metal.dt);
