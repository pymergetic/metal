/* pymergetic.metal.net.ip — link side: route table, interface table, ARP.
 * Everything here answers "which interface, which source address, which MAC";
 * the datagram itself is __wire__.c. */
#include "pymergetic/metal/net/ip/__priv__.h"

#include "pymergetic/metal/drivers/net.h"

#include <string.h>

static uint32_t mask_bits(uint32_t m) {
    uint32_t n = 0;
    while (m != 0) {
        n += m & 1u;
        m >>= 1;
    }
    return n;
}

static void rt_clear(void) {
    memset(pm_ip_rt, 0, sizeof(pm_ip_rt));
}

void pm_ip_rt_del_h(int32_t h) {
    uint32_t i;
    for (i = 0; i < PM_METAL_IP_RT_MAX; i++) {
        if (pm_ip_rt[i].used && pm_ip_rt[i].h == h) {
            pm_ip_rt[i].used = 0;
        }
    }
}

void pm_ip_rt_del(uint32_t dst_be, uint32_t mask_be) {
    uint32_t i;
    for (i = 0; i < PM_METAL_IP_RT_MAX; i++) {
        if (pm_ip_rt[i].used && pm_ip_rt[i].dst_be == dst_be && pm_ip_rt[i].mask_be == mask_be) {
            pm_ip_rt[i].used = 0;
        }
    }
}

void pm_ip_rt_upsert(uint32_t dst_be, uint32_t mask_be, uint32_t gw_be, int32_t h) {
    uint32_t i;
    int32_t slot = -1;
    for (i = 0; i < PM_METAL_IP_RT_MAX; i++) {
        if (pm_ip_rt[i].used && pm_ip_rt[i].dst_be == dst_be && pm_ip_rt[i].mask_be == mask_be) {
            pm_ip_rt[i].gw_be = gw_be;
            pm_ip_rt[i].h = h;
            return;
        }
        if (!pm_ip_rt[i].used && slot < 0) {
            slot = (int32_t)i;
        }
    }
    if (slot < 0) {
        return;
    }
    pm_ip_rt[slot].used = 1;
    pm_ip_rt[slot].dst_be = dst_be;
    pm_ip_rt[slot].mask_be = mask_be;
    pm_ip_rt[slot].gw_be = gw_be;
    pm_ip_rt[slot].h = h;
}

static const struct pm_metal_ip_rt *rt_match(uint32_t dst_be) {
    uint32_t i;
    uint32_t best = 0;
    const struct pm_metal_ip_rt *hit = NULL;
    const struct pm_metal_ip_rt *def = NULL;
    for (i = 0; i < PM_METAL_IP_RT_MAX; i++) {
        uint32_t bits;
        if (!pm_ip_rt[i].used) {
            continue;
        }
        if (pm_ip_rt[i].mask_be == 0) {
            def = &pm_ip_rt[i];
            continue;
        }
        if ((dst_be & pm_ip_rt[i].mask_be) != (pm_ip_rt[i].dst_be & pm_ip_rt[i].mask_be)) {
            continue;
        }
        bits = mask_bits(pm_ip_rt[i].mask_be);
        if (hit == NULL || bits >= best) {
            best = bits;
            hit = &pm_ip_rt[i];
        }
    }
    return hit != NULL ? hit : def;
}

static int32_t rt_lookup(uint32_t dst_be) {
    const struct pm_metal_ip_rt *rt = rt_match(dst_be);
    return rt != NULL ? rt->h : -1;
}

void pm_ip_l2_clear(void) {
    memset(pm_ip_l2, 0, sizeof(pm_ip_l2));
    pm_ip_l2_n = 0;
    pm_ip_l2_cur = -1;
    pm_ip_if_pending_be = 0;
    pm_ip_if_pending_mask = 0;
    rt_clear();
    pm_ip_arp_clear();
}

uint32_t pm_ip_l2_addr_of(int32_t h) {
    uint32_t i;
    for (i = 0; i < pm_ip_l2_n; i++) {
        if (pm_ip_l2[i].h == h) {
            return pm_ip_l2[i].addr_be;
        }
    }
    return 0;
}

uint32_t pm_ip_l2_mask_of(int32_t h) {
    uint32_t i;
    for (i = 0; i < pm_ip_l2_n; i++) {
        if (pm_ip_l2[i].h == h) {
            return pm_ip_l2[i].mask_be;
        }
    }
    return 0;
}

int32_t pm_ip_l2_has(int32_t h) {
    uint32_t i;
    for (i = 0; i < pm_ip_l2_n; i++) {
        if (pm_ip_l2[i].h == h) {
            return 1;
        }
    }
    return 0;
}

int32_t pm_ip_l2_h_for_addr(uint32_t addr_be) {
    uint32_t i;
    if (addr_be == 0) {
        return -1;
    }
    for (i = 0; i < pm_ip_l2_n; i++) {
        if (pm_ip_l2[i].addr_be == addr_be) {
            return pm_ip_l2[i].h;
        }
    }
    return -1;
}

int32_t pm_ip_if_up_mask(int32_t h, uint32_t addr_be, uint32_t mask_be) {
    uint32_t i;
    if (pm_ip_arena == NULL || addr_be == 0 || mask_be == 0 || !pm_ip_l2_has(h)) {
        return -1;
    }
    for (i = 0; i < pm_ip_l2_n; i++) {
        if (pm_ip_l2[i].h == h) {
            pm_ip_l2[i].addr_be = addr_be;
            pm_ip_l2[i].mask_be = mask_be;
            break;
        }
    }
    pm_ip_rt_upsert(addr_be & mask_be, mask_be, 0, h);
    if (pm_ip_l2_cur < 0 || pm_ip_l2_cur == h) {
        pm_ip_l2_cur = h;
        /* No gateway is known yet, so the default route stays on-link until
         * DHCP or pm_metal_net_ip_gw_set names one. */
        pm_ip_rt_upsert(0, 0, 0, h);
    }
    pm_ip_arp_announce(h);
    return 0;
}

void pm_ip_l2_apply_pending(void) {
    if (pm_ip_if_pending_be == 0 || pm_ip_l2_cur < 0) {
        return;
    }
    (void)pm_ip_if_up_mask(pm_ip_l2_cur, pm_ip_if_pending_be,
        pm_ip_if_pending_mask != 0 ? pm_ip_if_pending_mask : PM_METAL_IP_MASK24);
    pm_ip_if_pending_be = 0;
    pm_ip_if_pending_mask = 0;
}

static uint32_t ip_src_route(uint32_t dst) {
    int32_t h;
    uint32_t i;
    uint32_t addr;
    if (dst == pm_ip_lo_addr_be || dst == 0x7f000001u) {
        return pm_ip_lo_addr_be;
    }
    h = rt_lookup(dst);
    addr = pm_ip_l2_addr_of(h);
    if (addr != 0) {
        return addr;
    }
    for (i = 0; i < pm_ip_l2_n; i++) {
        if (pm_ip_l2[i].addr_be != 0) {
            return pm_ip_l2[i].addr_be;
        }
    }
    return pm_ip_lo_addr_be;
}

uint32_t pm_ip_src_for(const struct pm_metal_sock *s, uint32_t dst) {
    if (s != NULL && s->laddr_be != 0 && s->laddr_be != 0xffffffffu) {
        return s->laddr_be;
    }
    if (s != NULL && s->l2_h >= 0) {
        /* Pinned to an interface, so its address is the answer even when it has
         * none yet: a DHCP client must say 0.0.0.0, not borrow lo or a peer. */
        return pm_ip_l2_addr_of(s->l2_h);
    }
    return ip_src_route(dst);
}

int32_t pm_ip_route_out(int32_t h_hint, uint32_t src_be, uint32_t dst_be, uint32_t *hop_be) {
    const struct pm_metal_ip_rt *rt = rt_match(dst_be);
    uint32_t mask;
    int32_t h = -1;
    uint32_t hop = dst_be;
    if (h_hint >= 0 && pm_ip_l2_has(h_hint)) {
        h = h_hint;
    }
    if (h < 0) {
        h = pm_ip_l2_h_for_addr(src_be);
    }
    if (h < 0) {
        h = rt != NULL ? rt->h : -1;
    }
    if (h < 0) {
        h = pm_ip_l2_n != 0 ? pm_ip_l2[0].h : -1;
    }
    if (h < 0) {
        return -1;
    }
    /* Sharing a subnet with the destination means it is the neighbour itself;
     * otherwise the frame goes to the gateway, if the route names one. */
    mask = pm_ip_l2_mask_of(h);
    if (mask != 0 && ((pm_ip_l2_addr_of(h) & mask) == (dst_be & mask))) {
        hop = dst_be;
    } else if (rt != NULL && rt->gw_be != 0) {
        hop = rt->gw_be;
    }
    if (hop_be != NULL) {
        *hop_be = hop;
    }
    return h;
}

int32_t pm_ip_l2_addr_ours(uint32_t dst_be) {
    uint32_t i;
    if (dst_be == 0) {
        return 0;
    }
    for (i = 0; i < pm_ip_l2_n; i++) {
        if (pm_ip_l2[i].addr_be == dst_be) {
            return 1;
        }
    }
    return (pm_ip_if_pending_be != 0 && dst_be == pm_ip_if_pending_be) ? 1 : 0;
}


void pm_ip_eth_tx(int32_t h, const uint8_t dmac[6], uint16_t ethertype, const uint8_t *body,
    uint32_t len) {
    uint8_t frame[PM_METAL_IP_PKT_MAX + 14u];
    uint8_t smac[6];
    uint32_t flen;
    if (h < 0 || dmac == NULL || body == NULL || len == 0 || len > PM_METAL_IP_PKT_MAX) {
        return;
    }
    memcpy(frame, dmac, 6);
    memset(smac, 0, sizeof(smac));
    pm_metal_drivers_net_mac(h, smac);
    memcpy(frame + 6, smac, 6);
    pm_ip_write_be16(frame + 12, ethertype);
    memcpy(frame + 14, body, len);
    flen = len + 14u;
    if (flen < 60u) {
        memset(frame + flen, 0, 60u - flen);
        flen = 60u;
    }
    (void)pm_metal_drivers_net_tx(h, frame, (uint16_t)flen);
}

static const uint8_t arp_bcast[6] = { 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu };

static void arp_emit(int32_t h, uint16_t op, const uint8_t *dmac, const uint8_t *tha,
    uint32_t tpa_be) {
    uint8_t body[28];
    uint8_t mac[6];
    uint32_t spa = pm_ip_l2_addr_of(h);
    if (spa == 0) {
        spa = pm_ip_if_pending_be;
    }
    memset(body, 0, sizeof(body));
    memset(mac, 0, sizeof(mac));
    pm_metal_drivers_net_mac(h, mac);
    pm_ip_write_be16(body, 1);
    pm_ip_write_be16(body + 2, 0x0800);
    body[4] = 6;
    body[5] = 4;
    pm_ip_write_be16(body + 6, op);
    memcpy(body + 8, mac, 6);
    pm_ip_write_be32(body + 14, spa);
    if (tha != NULL) {
        memcpy(body + 18, tha, 6);
    }
    pm_ip_write_be32(body + 24, tpa_be);
    pm_ip_eth_tx(h, dmac, 0x0806u, body, sizeof(body));
}

void pm_ip_arp_clear(void) {
    memset(pm_ip_arp, 0, sizeof(pm_ip_arp));
}

/* A slot belongs to a neighbour from the moment it is claimed, whatever its
 * state: the datagram queued while we ask must survive the ask. addr_be == 0 is
 * the only mark of an unclaimed slot. */
static struct pm_metal_ip_arp *arp_find(int32_t h, uint32_t addr_be) {
    uint32_t i;
    if (addr_be == 0) {
        return NULL;
    }
    for (i = 0; i < PM_METAL_IP_ARP_MAX; i++) {
        if (pm_ip_arp[i].addr_be == addr_be && pm_ip_arp[i].h == h) {
            return &pm_ip_arp[i];
        }
    }
    return NULL;
}

/* The entry for this neighbour, made if need be. A full table gives up its
 * least recently touched entry — with any datagram still waiting on it. */
static struct pm_metal_ip_arp *arp_slot(int32_t h, uint32_t addr_be) {
    struct pm_metal_ip_arp *e = arp_find(h, addr_be);
    struct pm_metal_ip_arp *old = NULL;
    uint32_t i;
    if (e != NULL) {
        return e;
    }
    for (i = 0; i < PM_METAL_IP_ARP_MAX; i++) {
        if (pm_ip_arp[i].addr_be == 0) {
            e = &pm_ip_arp[i];
            break;
        }
        if (old == NULL || pm_ip_arp[i].at_us < old->at_us) {
            old = &pm_ip_arp[i];
        }
    }
    if (e == NULL) {
        e = old;
    }
    memset(e, 0, sizeof(*e));
    e->h = h;
    e->addr_be = addr_be;
    return e;
}

int32_t pm_ip_arp_lookup(int32_t h, uint32_t addr_be, uint8_t mac[6]) {
    struct pm_metal_ip_arp *e = arp_find(h, addr_be);
    if (e == NULL || e->state != ARP_LIVE || mac == NULL) {
        return 0;
    }
    if (pm_metal_async_mono_us() >= e->at_us) {
        e->state = ARP_FREE;
        return 0;
    }
    memcpy(mac, e->mac, 6);
    return 1;
}

void pm_ip_arp_learn(int32_t h, uint32_t addr_be, const uint8_t mac[6]) {
    struct pm_metal_ip_arp *e;
    if (h < 0 || addr_be == 0 || addr_be == 0xffffffffu || mac == NULL) {
        return;
    }
    e = arp_slot(h, addr_be);
    e->state = ARP_LIVE;
    e->tries = 0;
    e->at_us = pm_metal_async_mono_us() + PM_METAL_IP_ARP_TTL_US;
    memcpy(e->mac, mac, 6);
    if (e->pend_len != 0) {
        uint32_t len = e->pend_len;
        e->pend_len = 0;
        pm_ip_eth_tx(h, e->mac, 0x0800u, e->pend, len);
    }
}

void pm_ip_arp_ask(int32_t h, uint32_t addr_be) {
    struct pm_metal_ip_arp *e;
    if (h < 0 || addr_be == 0 || addr_be == 0xffffffffu) {
        return;
    }
    e = arp_slot(h, addr_be);
    if (e->state == ARP_LIVE) {
        return;
    }
    e->state = ARP_ASKING;
    e->tries++;
    e->at_us = pm_metal_async_mono_us() + PM_METAL_IP_ARP_RETRY_US;
    arp_emit(h, 1u, arp_bcast, NULL, addr_be);
}

void pm_ip_arp_queue(int32_t h, uint32_t addr_be, const uint8_t *pkt, uint32_t len) {
    struct pm_metal_ip_arp *e;
    if (h < 0 || pkt == NULL || len == 0 || len > PM_METAL_IP_ARP_PEND) {
        return;
    }
    e = arp_slot(h, addr_be);
    memcpy(e->pend, pkt, len);
    e->pend_len = len;
}

void pm_ip_arp_tick(void) {
    uint64_t now = pm_metal_async_mono_us();
    uint32_t i;
    for (i = 0; i < PM_METAL_IP_ARP_MAX; i++) {
        struct pm_metal_ip_arp *e = &pm_ip_arp[i];
        if (e->state == ARP_FREE || now < e->at_us) {
            continue;
        }
        if (e->state == ARP_LIVE) {
            e->state = ARP_FREE;
            continue;
        }
        if (e->tries >= PM_METAL_IP_ARP_TRIES) {
            /* Nobody answered. Release the slot and whatever waited on it, so a
             * later send starts a fresh round rather than inheriting this one. */
            memset(e, 0, sizeof(*e));
            continue;
        }
        e->tries++;
        e->at_us = now + PM_METAL_IP_ARP_RETRY_US;
        arp_emit(e->h, 1u, arp_bcast, NULL, e->addr_be);
    }
}

void pm_ip_arp_input(int32_t h, const uint8_t *frame, uint16_t len) {
    uint16_t op;
    uint32_t spa;
    uint32_t tpa;
    uint32_t ours;
    if (h < 0 || frame == NULL || len < 42u) {
        return;
    }
    if (pm_ip_read_be16(frame + 14) != 1u || pm_ip_read_be16(frame + 16) != 0x0800u) {
        return;
    }
    if (frame[18] != 6u || frame[19] != 4u) {
        return;
    }
    op = pm_ip_read_be16(frame + 20);
    spa = pm_ip_read_be32(frame + 28);
    /* Both opcodes carry the sender's binding, so a request teaches us as much
     * as a reply — and a reply is the only way our own asks ever resolve. */
    pm_ip_arp_learn(h, spa, frame + 22);
    if (op != 1u) {
        return;
    }
    tpa = pm_ip_read_be32(frame + 38);
    ours = pm_ip_l2_addr_of(h);
    if (ours == 0 || tpa != ours) {
        if (!pm_ip_l2_addr_ours(tpa)) {
            return;
        }
        ours = tpa;
    }
    arp_emit(h, 2u, frame + 6, frame + 22, spa);
}

void pm_ip_arp_announce(int32_t h) {
    uint32_t ours;
    if (h < 0) {
        return;
    }
    ours = pm_ip_l2_addr_of(h);
    if (ours == 0) {
        return;
    }
    arp_emit(h, 1u, arp_bcast, NULL, ours);
}
