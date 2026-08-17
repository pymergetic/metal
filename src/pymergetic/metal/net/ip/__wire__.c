/* pymergetic.metal.net.ip — the datagram itself: frame in, IPv4 demux to
 * ICMP echo / UDP / TCP, and IPv4 out (lo short-circuit or ethernet tx). */
#include "pymergetic/metal/net/ip/__priv__.h"

#include "pymergetic/metal/net/ip/__exports__.h"

#include "pymergetic/metal/drivers/net.h"

#include <string.h>

uint16_t pm_ip_csum(const uint8_t *p, uint32_t n) {
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
    if (len >= 14u) {
        memcpy(pm_ip_eth_dmac, frame + 6, 6);
        pm_ip_eth_dmac_ok = 1;
        et = (uint16_t)((frame[12] << 8) | frame[13]);
        if (et == 0x0806u) {
            pm_ip_arp_reply(h, frame, len);
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
    if (pm_ip_lo_up && len >= 20u) {
        uint32_t dst = pm_ip_read_be32(pkt + 16);
        if (dst == pm_ip_lo_addr_be || dst == 0x7f000001u) {
            ip_input(pkt, len);
            return;
        }
    }
    if (len >= 20u) {
        int32_t h = pm_ip_l2_tx_for_pkt(pm_ip_read_be32(pkt + 12), pm_ip_read_be32(pkt + 16));
        if (h >= 0) {
            uint8_t frame[PM_METAL_IP_PKT_MAX + 14u];
            uint8_t srcmac[6];
            uint32_t flen;
            if (len > PM_METAL_IP_PKT_MAX) {
                return;
            }
            if (pm_ip_eth_dmac_ok) {
                memcpy(frame, pm_ip_eth_dmac, 6);
            } else {
                memset(frame, 0xff, 6);
            }
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

static void ip_input(const uint8_t *pkt, uint32_t len) {
    if (len < 20u || (pkt[0] >> 4) != 4) {
        return;
    }
    uint32_t ihl = (uint32_t)(pkt[0] & 0x0fu) * 4u;
    if (ihl < 20u || len < ihl) {
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
        uint32_t copy = l4len > PM_METAL_IP_RX_MAX ? PM_METAL_IP_RX_MAX : l4len;
        memcpy(pm_ip_ping_out, reply + ihl, copy);
        pm_ip_ping_len = copy;
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
            if (!pcb->used || pcb->kind != SK_UDP || !pcb->bound) {
                continue;
            }
            if (pcb->lport != dport) {
                continue;
            }
            if (pcb->l2_h >= 0 && pm_ip_rx_l2 >= 0 && pcb->l2_h != pm_ip_rx_l2) {
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
            pm_ip_sock_wake(pcb);
            return;
        }
    }
}
