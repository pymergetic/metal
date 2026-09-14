/* pymergetic.metal.net.ip — TCP state machine. One rexmit slot per socket
 * with a fixed RTO (L2 sim drops packets); lo delivers inside the same xmit. */
#include "pymergetic/metal/net/ip/__priv__.h"

#include <string.h>

static int32_t tcp_emit(struct pm_metal_sock *s, uint8_t flags, const uint8_t *data, uint32_t dlen,
    uint32_t seq) {
    uint32_t th = 20u;
    uint32_t total = 20u + th + dlen;
    uint8_t pkt[PM_METAL_IP_PKT_MAX];
    if (total > PM_METAL_IP_PKT_MAX) {
        return -1;
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
    uint32_t wnd = s->rx_cap - s->rx_len;
    pm_ip_write_be16(pkt + 34, wnd > 0xffffu ? 0xffffu : (uint16_t)wnd);
    if (dlen != 0 && data != NULL) {
        memcpy(pkt + 40, data, dlen);
    }
    pm_ip_l4_stamp(pkt, total);
    return pm_ip_output_via(s->l2_h, pkt, total);
}

uint32_t pm_ip_tcp_payload_max(const struct pm_metal_sock *s) {
    uint32_t frame_max;
    uint32_t payload_max;
    uint32_t src;
    uint32_t dst;
    uint32_t hop;
    int32_t h;
    if (s == NULL) {
        return 0;
    }
    dst = s->raddr_be ? s->raddr_be : pm_ip_lo_addr_be;
    if (pm_ip_lo_up && (dst == pm_ip_lo_addr_be || dst == PM_METAL_IP_LO_BE)) {
        return PM_METAL_IP_TCP_MSS;
    }
    src = pm_ip_src_for(s, dst);
    h = pm_ip_route_out(s->l2_h, src, dst, &hop);
    (void)hop;
    if (h < 0) {
        return 0;
    }
    frame_max = pm_metal_drivers_net_frame_max(h);
    if (frame_max <= 14u + 20u + 20u) {
        return 0;
    }
    payload_max = frame_max - 14u - 20u - 20u;
    return payload_max < PM_METAL_IP_TCP_MSS ? payload_max : PM_METAL_IP_TCP_MSS;
}

int32_t pm_ip_tcp_xmit(struct pm_metal_sock *s, uint8_t flags, const uint8_t *data, uint32_t dlen) {
    uint32_t seq;
    uint32_t consumes;
    uint32_t old_rexmit_len;
    int32_t tx;
    if (s == NULL) return PM_METAL_NET_TX_ERROR;
    consumes = dlen + (((flags & (TCP_SYN | TCP_FIN)) != 0u) ? 1u : 0u);
    if (consumes != 0u && s->rexmit_len + consumes > s->rexmit_cap) {
        return PM_METAL_NET_TX_WAIT;
    }
    seq = s->snd_nxt;
    old_rexmit_len = s->rexmit_len;
    /* Reserve sequence space before emission. Loopback delivery is synchronous:
     * the peer's ACK can recursively enter tcp_input before tcp_emit returns. */
    if (consumes != 0u) {
        if (old_rexmit_len == 0u) {
            s->rexmit_seq = seq;
            s->rexmit_flags = flags;
            s->rexmit_at = pm_metal_coop_mono_us() + PM_METAL_IP_RTO_US;
            s->rexmit_tries = 0u;
        }
        if (dlen != 0u && data != NULL) {
            memcpy(s->rexmit + old_rexmit_len, data, dlen);
        }
        s->rexmit_len = old_rexmit_len + consumes;
        s->rexmit_end = s->rexmit_seq + s->rexmit_len;
        s->snd_nxt += consumes;
    }
    tx = tcp_emit(s, flags, data, dlen, seq);
    if (tx != PM_METAL_NET_TX_OK) {
        s->snd_nxt = seq;
        s->rexmit_len = old_rexmit_len;
        s->rexmit_end = old_rexmit_len != 0u ? s->rexmit_seq + old_rexmit_len : 0u;
        return tx;
    }
    /* Do not touch s after successful emission: synchronous loopback ACK/FIN
     * processing may have completed close and released the socket row. */
    return PM_METAL_NET_TX_OK;
}

int32_t pm_ip_tcp_close(struct pm_metal_sock *s) {
    int32_t tx;
    if (s == NULL || s->kind != SK_TCP) {
        return -1;
    }
    s->app_closed = 1u;
    if (s->tcp_error) {
        pm_ip_sock_drop(s->self_fd);
        return 1;
    }
    if (s->tcp_st != TCP_ESTAB && s->tcp_st != TCP_CLOSE_WAIT
        && s->tcp_st != TCP_FIN_WAIT
        && s->tcp_st != TCP_LAST_ACK) {
        pm_ip_sock_drop(s->self_fd);
        return 1;
    }
    if (!s->fin_sent) {
        int32_t fd = s->self_fd;
        uint32_t loopback_passive = s->peer_fin && pm_ip_lo_up
            && (s->raddr_be == pm_ip_lo_addr_be || s->raddr_be == PM_METAL_IP_LO_BE);
        if (s->snd_una != s->snd_nxt || s->rexmit_len != 0u) {
            return 0;
        }
        s->fin_sent = 1u;
        s->tcp_st = s->peer_fin ? TCP_LAST_ACK : TCP_FIN_WAIT;
        tx = pm_ip_tcp_xmit(s, (uint8_t)(TCP_FIN | TCP_ACK), NULL, 0);
        if (tx == PM_METAL_NET_TX_WAIT) {
            s->fin_sent = 0u;
            return 0;
        }
        if (tx != PM_METAL_NET_TX_OK) {
            pm_ip_sock_drop(s->self_fd);
            return -1;
        }
        /* The active loopback closer was already ACKed and released before
         * this passive close. Its FIN has no remaining row to ACK ours, but
         * delivery is in-process and cannot be lost, so release LAST_ACK here. */
        if (loopback_passive && pm_ip_sock_at(fd) == s) {
            pm_ip_sock_drop(fd);
        }
        /* A synchronous loopback ACK may already have released s. */
        return 0;
    }
    return 0;
}

void pm_ip_tcp_check_timeouts(void) {
    uint64_t now = pm_metal_coop_mono_us();
    uint32_t i;
    for (i = 0; i < pm_ip_sk_cap; i++) {
        struct pm_metal_sock *s = pm_ip_sk[i];
        int32_t tx;
        if (s == NULL || s->kind != SK_TCP) {
            continue;
        }
        if (s->app_closed && !s->fin_sent && s->snd_una == s->snd_nxt && s->rexmit_len == 0u) {
            (void)pm_ip_tcp_close(s);
            continue;
        }
        if (s->rexmit_len == 0u || now < s->rexmit_at) {
            continue;
        }
        {
            uint32_t ctl = ((s->rexmit_flags & (TCP_SYN | TCP_FIN)) != 0u) ? 1u : 0u;
            uint32_t dlen = s->rexmit_len - ctl;
            uint32_t mss = pm_ip_tcp_payload_max(s);
            if (dlen > mss) dlen = mss;
            tx = tcp_emit(s, s->rexmit_flags, s->rexmit, dlen, s->rexmit_seq);
        }
        if (tx == PM_METAL_NET_TX_OK) {
            s->rexmit_tries++;
        }
        if (s->rexmit_tries >= PM_METAL_IP_REXMIT_TRIES) {
            /* close() transfers ownership back to TCP while its FIN is pending.
             * No application can observe that fd again, so release it after the
             * bounded retry budget. A live application fd must remain allocated:
             * dropping that row here permits fd reuse before its coroutine wakes
             * (fd ABA). */
            if (s->app_closed) {
                pm_ip_sock_drop(s->self_fd);
                continue;
            }
            s->tcp_error = 1u;
            s->rexmit_len = 0u;
            s->rexmit_end = 0u;
            pm_ip_sock_wake(s);
            continue;
        }
        s->rexmit_at = now + PM_METAL_IP_RTO_US;
    }
}

static void tcp_queue_accept(struct pm_metal_sock *ls, int32_t child) {
    struct pm_metal_sock *c = pm_ip_sock_at(child);
    if (c == NULL) {
        return;
    }
    if (ls->accept_q != NULL && ls->accept_n < ls->accept_cap) {
        ls->accept_q[ls->accept_n++] = child;
        pm_ip_sock_wake(ls);
        return;
    }
    /* Full. This handshake is already complete, so saying nothing leaves the
     * peer holding a connection that is up and will never be answered — it
     * waits out its own timeout and reports an empty reply, which is how a
     * page load lost a script. Reset it instead: the peer learns now and can
     * come back, and the slot does not sit used for a child no accept() will
     * ever be handed. */
    pm_ip_tcp_xmit(c, (uint8_t)(TCP_RST | TCP_ACK), NULL, 0);
    pm_ip_sock_drop(child);
}

static struct pm_metal_sock *tcp_find(uint32_t src, uint16_t sport, uint32_t dst, uint16_t dport) {
    uint32_t i;
    struct pm_metal_sock *listen = NULL;
    for (i = 0; i < pm_ip_sk_cap; i++) {
        struct pm_metal_sock *s = pm_ip_sk[i];
        if (s == NULL || s->kind != SK_TCP) {
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
    /* A reset ends this connection now. Ignoring it left the socket sitting in
     * ESTAB with a peer that is gone: a reader parked forever, a sender firing
     * into nothing. Report it the way a closed peer is reported — readers get
     * the end of the stream once what already arrived is drained, senders an
     * error — and wake whoever was waiting. */
    if ((flags & TCP_RST) != 0 && s->tcp_st != TCP_LISTEN) {
        s->peer_fin = 1;
        s->tcp_st = TCP_CLOSE_WAIT;
        pm_ip_sock_wake(s);
        if (s->app_closed) {
            pm_ip_sock_drop(s->self_fd);
        }
        return;
    }
    if ((flags & TCP_ACK) != 0 && ack > s->snd_una && ack <= s->snd_nxt) {
        s->snd_una = ack;
        if (s->rexmit_len != 0 && s->snd_una > s->rexmit_seq) {
            uint32_t acked = s->snd_una - s->rexmit_seq;
            if (acked >= s->rexmit_len) {
                s->rexmit_len = 0;
                s->rexmit_end = 0;
            } else {
                memmove(s->rexmit, s->rexmit + acked, s->rexmit_len - acked);
                s->rexmit_len -= acked;
                s->rexmit_seq += acked;
                s->rexmit_end = s->rexmit_seq + s->rexmit_len;
            }
            s->rexmit_tries = 0;
            s->rexmit_at = pm_metal_coop_mono_us() + PM_METAL_IP_RTO_US;
        }
        /* An ACK freed send-window space; wake any coroutine parked on a full
         * window (streaming server responses) so it can push the next chunk. */
        pm_ip_sock_wake(s);
        if (s->fin_sent && s->snd_una == s->snd_nxt
            && (s->tcp_st == TCP_FIN_WAIT || s->tcp_st == TCP_LAST_ACK)
            && (flags & TCP_FIN) == 0u) {
            pm_ip_sock_drop(s->self_fd);
            return;
        } else if (s->app_closed && s->snd_una == s->snd_nxt) {
            (void)pm_ip_tcp_close(s);
            return;
        }
    }
    /* Track the peer's advertised window so a streamed (large) response stops
     * sending when the peer's receive buffer is full instead of firing MSS
     * chunks that are then dropped as out-of-order (rx space NAK'd). Every
     * ACK-carrying segment reports the peer's current window; a window update
     * (e.g. the peer drained its rx) re-opens send space, so wake too. */
    if ((flags & TCP_ACK) != 0) {
        uint16_t pw = pm_ip_read_be16(th + 14);
        uint32_t w = (uint32_t)pw;
        if (w > s->snd_wnd) {
            s->snd_wnd = w;
            pm_ip_sock_wake(s);
        } else if (w < s->snd_wnd) {
            s->snd_wnd = w;
        }
    }
    if (s->tcp_st == TCP_LISTEN && (flags & TCP_SYN) != 0 && (flags & TCP_ACK) == 0) {
        int32_t cfd = pm_ip_sock_alloc(SK_TCP);
        if (cfd < 0) {
            return;
        }
        struct pm_metal_sock *c = pm_ip_sock_at(cfd);
        if (c == NULL) {
            return;
        }
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
        c->listen_fd = s->self_fd;
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
            struct pm_metal_sock *ls = pm_ip_sock_at(s->listen_fd);
            if (ls != NULL) {
                tcp_queue_accept(ls, s->self_fd);
            }
        }
        return;
    }
    if (s->tcp_st == TCP_ESTAB || s->tcp_st == TCP_FIN_WAIT || s->tcp_st == TCP_CLOSE_WAIT) {
        if (dlen != 0) {
            if (seq != s->rcv_nxt) {
                pm_ip_tcp_xmit(s, TCP_ACK, NULL, 0);
                return;
            }
            uint32_t room = s->rx_cap - s->rx_len;
            uint32_t n = dlen < room ? dlen : room;
            memcpy(s->rx + s->rx_len, data, n);
            s->rx_len += n;
            s->rcv_nxt += n;
            pm_ip_tcp_xmit(s, TCP_ACK, NULL, 0);
            pm_ip_sock_wake(s);
        }
        /* FIN consumes one seq after the payload. A FIN that arrives before
         * the missing bytes (or with a hole) is not EOF — ACK rcv_nxt so the
         * peer retransmits the hole. HTTP/1.0 Connection: close often delivers
         * headers, then body, then FIN; treating the FIN first used to make
         * recv return -2 with only the headers in acc. */
        if ((flags & TCP_FIN) != 0) {
            if (seq + dlen != s->rcv_nxt) {
                pm_ip_tcp_xmit(s, TCP_ACK, NULL, 0);
                return;
            }
            s->rcv_nxt += 1u;
            s->peer_fin = 1;
            if (s->tcp_st == TCP_ESTAB) {
                s->tcp_st = TCP_CLOSE_WAIT;
            }
            pm_ip_tcp_xmit(s, TCP_ACK, NULL, 0);
            pm_ip_sock_wake(s);
            if (s->app_closed && s->fin_sent && s->snd_una == s->snd_nxt) {
                pm_ip_sock_drop(s->self_fd);
                return;
            }
            if (s->app_closed && !s->fin_sent) {
                (void)pm_ip_tcp_close(s);
            }
        }
    }
}
