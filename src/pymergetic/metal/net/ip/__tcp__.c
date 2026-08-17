/* pymergetic.metal.net.ip — TCP state machine. One rexmit slot per socket
 * with a fixed RTO (L2 sim drops packets); lo delivers inside the same xmit. */
#include "pymergetic/metal/net/ip/__priv__.h"

#include <string.h>

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
    pm_ip_write_be16(pkt + 2, (uint16_t)total);
    pkt[8] = 64;
    pkt[9] = 6;
    pm_ip_write_be32(pkt + 12, pm_ip_src_for(s, s->raddr_be ? s->raddr_be : pm_ip_lo_addr_be));
    pm_ip_write_be32(pkt + 16, s->raddr_be ? s->raddr_be : pm_ip_lo_addr_be);
    uint16_t cs = pm_ip_csum(pkt, 20);
    pm_ip_write_be16(pkt + 10, cs);
    pm_ip_write_be16(pkt + 20, s->lport);
    pm_ip_write_be16(pkt + 22, s->rport);
    pm_ip_write_be32(pkt + 24, seq);
    pm_ip_write_be32(pkt + 28, s->rcv_nxt);
    pkt[32] = 0x50;
    pkt[33] = flags;
    pm_ip_write_be16(pkt + 34, 4096);
    if (dlen != 0 && data != NULL) {
        memcpy(pkt + 40, data, dlen);
    }
    pm_ip_l4_stamp(pkt, total);
    pm_ip_output(pkt, total);
}

void pm_ip_tcp_xmit(struct pm_metal_sock *s, uint8_t flags, const uint8_t *data, uint32_t dlen) {
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

void pm_ip_tcp_check_timeouts(void) {
    uint64_t now = pm_metal_async_mono_us();
    uint32_t i;
    for (i = 0; i < PM_METAL_IP_SOCK_MAX; i++) {
        struct pm_metal_sock *s = &pm_ip_sk[i];
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
        pm_ip_sock_wake(ls);
    }
}

static struct pm_metal_sock *tcp_find(uint32_t src, uint16_t sport, uint32_t dst, uint16_t dport) {
    uint32_t i;
    struct pm_metal_sock *listen = NULL;
    for (i = 0; i < PM_METAL_IP_SOCK_MAX; i++) {
        struct pm_metal_sock *s = &pm_ip_sk[i];
        if (!s->used || s->kind != SK_TCP) {
            continue;
        }
        if (s->tcp_st == TCP_LISTEN && s->lport == dport) {
            if ((s->laddr_be == 0 || s->laddr_be == 0xffffffffu || s->laddr_be == dst)
                && (s->l2_h < 0 || pm_ip_rx_l2 < 0 || s->l2_h == pm_ip_rx_l2)) {
                listen = s;
            }
            continue;
        }
        if (s->lport == dport && s->rport == sport && s->raddr_be == src) {
            if (s->l2_h >= 0 && pm_ip_rx_l2 >= 0 && s->l2_h != pm_ip_rx_l2) {
                continue;
            }
            return s;
        }
    }
    return listen;
}

void pm_ip_tcp_input(uint32_t src, uint32_t dst, const uint8_t *th, uint32_t thlen) {
    if (thlen < 20u) {
        return;
    }
    uint16_t sport = pm_ip_read_be16(th);
    uint16_t dport = pm_ip_read_be16(th + 2);
    uint32_t seq = pm_ip_read_be32(th + 4);
    uint32_t ack = pm_ip_read_be32(th + 8);
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
        int32_t cfd = pm_ip_sock_alloc(SK_TCP);
        if (cfd < 0) {
            return;
        }
        struct pm_metal_sock *c = &pm_ip_sk[cfd];
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
        c->listen_fd = (int32_t)(s - pm_ip_sk);
        pm_ip_tcp_xmit(c, (uint8_t)(TCP_SYN | TCP_ACK), NULL, 0);
        return;
    }
    if (s->tcp_st == TCP_SYN_SENT && (flags & TCP_SYN) != 0 && (flags & TCP_ACK) != 0) {
        s->rcv_nxt = seq + 1u;
        s->tcp_st = TCP_ESTAB;
        s->snd_una = s->snd_nxt;
        pm_ip_tcp_xmit(s, TCP_ACK, NULL, 0);
        pm_ip_sock_wake(s);
        return;
    }
    if (s->tcp_st == TCP_SYN_RCVD && (flags & TCP_ACK) != 0) {
        s->tcp_st = TCP_ESTAB;
        s->snd_una = s->snd_nxt;
        if (s->listen_fd >= 0) {
            tcp_queue_accept(&pm_ip_sk[s->listen_fd], (int32_t)(s - pm_ip_sk));
        }
        return;
    }
    if (s->tcp_st == TCP_ESTAB || s->tcp_st == TCP_FIN_WAIT || s->tcp_st == TCP_CLOSE_WAIT) {
        if (dlen != 0) {
            if (seq != s->rcv_nxt) {
                pm_ip_tcp_xmit(s, TCP_ACK, NULL, 0);
                return;
            }
            uint32_t room = PM_METAL_IP_RX_MAX - s->rx_len;
            uint32_t n = dlen < room ? dlen : room;
            memcpy(s->rx + s->rx_len, data, n);
            s->rx_len += n;
            s->rcv_nxt += n;
            pm_ip_tcp_xmit(s, TCP_ACK, NULL, 0);
            pm_ip_sock_wake(s);
        }
        if ((flags & TCP_FIN) != 0) {
            s->rcv_nxt += 1u;
            s->peer_fin = 1;
            if (s->tcp_st == TCP_ESTAB) {
                s->tcp_st = TCP_CLOSE_WAIT;
            }
            pm_ip_tcp_xmit(s, TCP_ACK, NULL, 0);
            pm_ip_sock_wake(s);
        }
    }
}
