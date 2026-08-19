/* pymergetic.metal.net.ip — the datagram itself: frame in, IPv4 demux to
 * ICMP echo / UDP / TCP, and IPv4 out (lo short-circuit or ethernet tx). */
#include "pymergetic/metal/net/ip/__priv__.h"

#include "pymergetic/metal/net/ip/__exports__.h"

#include "pymergetic/metal/drivers/net.h"

#include <string.h>

static uint32_t csum_add(const uint8_t *p, uint32_t n, uint32_t s) {
    while (n > 1u) {
        s += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        n -= 2;
    }
    if (n != 0) {
        s += (uint32_t)p[0] << 8;
    }
    return s;
}

static uint16_t csum_fold(uint32_t s) {
    while ((s >> 16) != 0) {
        s = (s & 0xffffu) + (s >> 16);
    }
    return (uint16_t)~s;
}

uint16_t pm_ip_csum(const uint8_t *p, uint32_t n) {
    return csum_fold(csum_add(p, n, 0));
}

uint16_t pm_ip_l4_csum(const uint8_t *pkt, uint32_t total) {
    uint32_t ihl = (uint32_t)(pkt[0] & 0x0fu) * 4u;
    uint32_t l4len;
    uint32_t s;
    if (total < ihl + 8u) {
        return 0;
    }
    l4len = total - ihl;
    s = csum_add(pkt + 12, 8u, 0);
    s += (uint32_t)pkt[9];
    s += l4len;
    return csum_fold(csum_add(pkt + ihl, l4len, s));
}

/* Write the transport checksum of a packet we are about to send. Leaving it at
 * zero is what kept this stack on loopback: every real peer drops a TCP segment
 * whose checksum does not add up, and only UDP may spell "unchecked" as 0. */
void pm_ip_l4_stamp(uint8_t *pkt, uint32_t total) {
    uint32_t ihl = (uint32_t)(pkt[0] & 0x0fu) * 4u;
    uint8_t proto = pkt[9];
    uint32_t at;
    uint16_t cs;
    if (proto == 6u) {
        at = ihl + 16u;
    } else if (proto == 17u) {
        at = ihl + 6u;
    } else {
        return;
    }
    if (total < at + 2u) {
        return;
    }
    pkt[at] = 0;
    pkt[at + 1u] = 0;
    cs = pm_ip_l4_csum(pkt, total);
    if (cs == 0 && proto == 17u) {
        cs = 0xffffu;
    }
    pm_ip_write_be16(pkt + at, cs);
}

static int32_t l4_csum_ok(const uint8_t *pkt, uint32_t total) {
    uint32_t ihl = (uint32_t)(pkt[0] & 0x0fu) * 4u;
    uint8_t proto = pkt[9];
    uint32_t at;
    if (proto == 6u) {
        at = ihl + 16u;
    } else if (proto == 17u) {
        at = ihl + 6u;
    } else {
        return 1;
    }
    if (total < at + 2u) {
        return 0;
    }
    /* A zero UDP checksum means the sender computed none. */
    if (proto == 17u && pm_ip_read_be16(pkt + at) == 0) {
        return 1;
    }
    return pm_ip_l4_csum(pkt, total) == 0 ? 1 : 0;
}

uint32_t pm_ip_read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void pm_ip_write_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

void pm_ip_write_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

uint16_t pm_ip_read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void ip_input(const uint8_t *pkt, uint32_t len);

int32_t pm_metal_net_ip_rx_from(int32_t h, const uint8_t *frame, uint16_t len) {
    const uint8_t *pkt = frame;
    uint32_t n = len;
    int32_t prev = pm_ip_rx_l2;
    uint16_t et;
    if (frame == NULL || len == 0) {
        return -1;
    }
    pm_ip_rx_l2 = h;
    pm_metal_drivers_net_count_rx(h);
    if (len >= 14u) {
        et = (uint16_t)((frame[12] << 8) | frame[13]);
        if (et == 0x0806u) {
            pm_ip_arp_input(h, frame, len);
            pm_ip_rx_l2 = prev;
            return 0;
        }
        if (et == 0x0800u) {
            pkt = frame + 14;
            n = (uint32_t)len - 14u;
        }
    }
    ip_input(pkt, n);
    pm_ip_rx_l2 = prev;
    return 0;
}

int32_t pm_metal_net_ip_rx(const uint8_t *frame, uint16_t len) {
    return pm_metal_net_ip_rx_from(-1, frame, len);
}

void pm_ip_output(const uint8_t *pkt, uint32_t len) {
    pm_ip_output_via(-1, pkt, len);
}

void pm_ip_output_via(int32_t h_hint, const uint8_t *pkt, uint32_t len) {
    uint32_t dst;
    uint32_t hop = 0;
    uint32_t mask;
    int32_t h;
    uint8_t dmac[6];
    if (len < 20u || len > PM_METAL_IP_PKT_MAX) {
        return;
    }
    dst = pm_ip_read_be32(pkt + 16);
    if (pm_ip_lo_up && (dst == pm_ip_lo_addr_be || dst == 0x7f000001u)) {
        /* Loopback: the segment arrives "on" no physical wire, so deliver it to
         * any-l2 sockets (a listener bound on lo must not be gated by a stale
         * pm_ip_rx_l2 left over from a real NIC's poll). Save/swap/restore the
         * rx-l2 filter so nested tcp_input matches the loopback listener. */
        int32_t prev_l2 = pm_ip_rx_l2;
        pm_ip_rx_l2 = -1;
        ip_input(pkt, len);
        pm_ip_rx_l2 = prev_l2;
        return;
    }
    if (pm_ip_lo_up && (dst & 0xf0000000u) == 0xe0000000u) {
        /* Multicast on loopback: the joined subscribers live in this stack, so
         * loop the datagram back through ip_input instead of building an
         * ethernet frame (there is no NIC on lo). This is the userspace-stack
         * equivalent of the kernel looping multicast back to local members. */
        int32_t prev_l2 = pm_ip_rx_l2;
        pm_ip_rx_l2 = -1;
        ip_input(pkt, len);
        pm_ip_rx_l2 = prev_l2;
        return;
    }
    h = pm_ip_route_out(h_hint, pm_ip_read_be32(pkt + 12), dst, &hop);
    if (h < 0) {
        return;
    }
    mask = pm_ip_l2_mask_of(h);
    if ((dst & 0xf0000000u) == 0xe0000000u) {
        /* IP multicast MAC: 01:00:5e:00:00:00 | low 23 bits of the group. */
        uint32_t lo23 = dst & 0x007fffffu;
        dmac[0] = 0x01;
        dmac[1] = 0x00;
        dmac[2] = 0x5e;
        dmac[3] = (uint8_t)(lo23 >> 16);
        dmac[4] = (uint8_t)(lo23 >> 8);
        dmac[5] = (uint8_t)lo23;
    } else if (dst == 0xffffffffu || (mask != 0 && (dst | mask) == 0xffffffffu)) {
        memset(dmac, 0xff, sizeof(dmac));
    } else if (pm_ip_l2_addr_ours(dst)) {
        /* Our own address on a wire that loops back to us (sim, a tunnel): the
         * frame is addressed to this very NIC, so there is nobody to ask. */
        memset(dmac, 0, sizeof(dmac));
        pm_metal_drivers_net_mac(h, dmac);
    } else if (!pm_ip_arp_lookup(h, hop, dmac)) {
        /* Hold the datagram, ask, and send it from pm_ip_arp_learn when the
         * neighbour answers. Guessing a MAC is what the old code did. */
        pm_ip_arp_queue(h, hop, pkt, len);
        pm_ip_arp_ask(h, hop);
        return;
    }
    pm_ip_eth_tx(h, dmac, 0x0800u, pkt, len);
}

static void ip_input(const uint8_t *pkt, uint32_t len) {
    if (len < 20u || (pkt[0] >> 4) != 4) {
        return;
    }
    uint32_t ihl = (uint32_t)(pkt[0] & 0x0fu) * 4u;
    if (ihl < 20u || len < ihl) {
        return;
    }
    /* A trailer-padded frame (every ethernet frame under 60 bytes) carries more
     * bytes than the header claims, and the transport checksum is over the
     * claimed length only. */
    {
        uint32_t total = pm_ip_read_be16(pkt + 2);
        if (total >= ihl && total <= len) {
            len = total;
        }
    }
    if (pm_ip_csum(pkt, ihl) != 0 || !l4_csum_ok(pkt, len)) {
        return;
    }
    uint32_t dst = pm_ip_read_be32(pkt + 16);
    uint32_t src = pm_ip_read_be32(pkt + 12);
    uint32_t ours = 0;
    if (pm_ip_lo_up && (dst == pm_ip_lo_addr_be || dst == 0x7f000001u)) {
        ours = 1;
    }
    if (pm_ip_l2_addr_ours(dst)) {
        ours = 1;
    }
    if (dst == 0xffffffffu) {
        ours = 1;
    }
    if ((dst & 0xf0000000u) == 0xe0000000u && pm_ip_mcast_joined(dst)) {
        ours = 1;
    }
    if (!ours) {
        return;
    }
    uint8_t proto = pkt[9];
    const uint8_t *l4 = pkt + ihl;
    uint32_t l4len = len - ihl;
    if (proto == 1 && l4len >= 8u && l4[0] == 0) {
        /* Echo reply. Only the answer to our own outstanding ping counts, so a
         * stray reply cannot satisfy pm_metal_net_ip_ping4. */
        if (pm_ip_read_be16(l4 + 4) == pm_ip_ping_id) {
            uint32_t copy = l4len > PM_METAL_IP_RX_MAX ? PM_METAL_IP_RX_MAX : l4len;
            memcpy(pm_ip_ping_out, l4, copy);
            pm_ip_ping_len = copy;
        }
        return;
    }
    if (proto == 1 && l4len >= 8u && l4[0] == 8) {
        uint8_t reply[PM_METAL_IP_PKT_MAX];
        if (len > PM_METAL_IP_PKT_MAX || dst == 0xffffffffu) {
            return;
        }
        memcpy(reply, pkt, len);
        pm_ip_write_be32(reply + 12, dst);
        pm_ip_write_be32(reply + 16, src);
        reply[10] = 0;
        reply[11] = 0;
        uint16_t cs = pm_ip_csum(reply, ihl);
        pm_ip_write_be16(reply + 10, cs);
        reply[ihl] = 0;
        reply[ihl + 2] = 0;
        reply[ihl + 3] = 0;
        cs = pm_ip_csum(reply + ihl, l4len);
        pm_ip_write_be16(reply + ihl + 2, cs);
        /* Our own ping to ourselves lands back here as the echo reply above,
         * so the answering side has nothing to record. */
        pm_ip_output(reply, len);
        return;
    }
    if (proto == 6) {
        pm_ip_tcp_input(src, dst, l4, l4len);
        return;
    }
    if (proto == 17 && l4len >= 8u) {
        uint16_t dport = pm_ip_read_be16(l4 + 2);
        uint16_t sport = pm_ip_read_be16(l4);
        const uint8_t *payload = l4 + 8;
        uint32_t plen = l4len - 8u;
        uint32_t i;
        for (i = 0; i < PM_METAL_IP_SOCK_MAX; i++) {
            struct pm_metal_sock *pcb = &pm_ip_sk[i];
            uint32_t j;
            uint32_t member = 0;
            if (!pcb->used || pcb->kind != SK_UDP || !pcb->bound) {
                continue;
            }
            if (pcb->lport != dport) {
                continue;
            }
            if (pcb->l2_h >= 0 && pm_ip_rx_l2 >= 0 && pcb->l2_h != pm_ip_rx_l2) {
                continue;
            }
            /* Unicast: a non-wildcard bound address must equal the dst. */
            if (pcb->laddr_be != 0 && pcb->laddr_be != 0xffffffffu && pcb->laddr_be != dst) {
                continue;
            }
            if (pcb->laddr_be == 0 || pcb->laddr_be == 0xffffffffu || pcb->laddr_be == dst) {
                member = 1;
            }
            if (!member && (dst & 0xf0000000u) == 0xe0000000u) {
                for (j = 0; j < pcb->mcast_n; j++) {
                    if (pcb->mcast_be[j] == dst) {
                        member = 1;
                        break;
                    }
                }
            }
            if (!member) {
                continue;
            }
            if (plen > PM_METAL_IP_RX_MAX) {
                plen = PM_METAL_IP_RX_MAX;
            }
            memcpy(pcb->rx, payload, plen);
            pcb->rx_len = plen;
            pcb->rx_addr_be = src;
            pcb->rx_port = sport;
            pm_ip_sock_wake(pcb);
            return;
        }
    }
}
