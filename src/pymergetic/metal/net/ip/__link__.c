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

void pm_ip_rt_upsert(uint32_t dst_be, uint32_t mask_be, int32_t h) {
    uint32_t i;
    int32_t slot = -1;
    for (i = 0; i < PM_METAL_IP_RT_MAX; i++) {
        if (pm_ip_rt[i].used && pm_ip_rt[i].dst_be == dst_be && pm_ip_rt[i].mask_be == mask_be) {
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
    pm_ip_rt[slot].h = h;
}

static int32_t rt_lookup(uint32_t dst_be) {
    uint32_t i;
    uint32_t best = 0;
    int32_t h = -1;
    int32_t def = -1;
    for (i = 0; i < PM_METAL_IP_RT_MAX; i++) {
        uint32_t bits;
        if (!pm_ip_rt[i].used) {
            continue;
        }
        if (pm_ip_rt[i].mask_be == 0) {
            def = pm_ip_rt[i].h;
            continue;
        }
        if ((dst_be & pm_ip_rt[i].mask_be) != (pm_ip_rt[i].dst_be & pm_ip_rt[i].mask_be)) {
            continue;
        }
        bits = mask_bits(pm_ip_rt[i].mask_be);
        if (h < 0 || bits >= best) {
            best = bits;
            h = pm_ip_rt[i].h;
        }
    }
    return h >= 0 ? h : def;
}

void pm_ip_l2_clear(void) {
    memset(pm_ip_l2, 0, sizeof(pm_ip_l2));
    pm_ip_l2_n = 0;
    pm_ip_l2_cur = -1;
    pm_ip_if_pending_be = 0;
    pm_ip_if_pending_mask = 0;
    rt_clear();
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
    pm_ip_rt_upsert(addr_be & mask_be, mask_be, h);
    if (pm_ip_l2_cur < 0 || pm_ip_l2_cur == h) {
        pm_ip_l2_cur = h;
        pm_ip_rt_upsert(0, 0, h);
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
    uint32_t addr;
    if (s != NULL && s->laddr_be != 0 && s->laddr_be != 0xffffffffu) {
        return s->laddr_be;
    }
    if (s != NULL && s->l2_h >= 0) {
        addr = pm_ip_l2_addr_of(s->l2_h);
        if (addr != 0) {
            return addr;
        }
    }
    return ip_src_route(dst);
}

int32_t pm_ip_l2_tx_for_pkt(uint32_t src_be, uint32_t dst_be) {
    int32_t h;
    h = pm_ip_l2_h_for_addr(src_be);
    if (h >= 0) {
        return h;
    }
    h = rt_lookup(dst_be);
    if (h >= 0) {
        return h;
    }
    return pm_ip_l2_n != 0 ? pm_ip_l2[0].h : -1;
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

void pm_ip_arp_reply(int32_t h, const uint8_t *frame, uint16_t len) {
    uint8_t out[60];
    uint8_t mac[6];
    uint32_t tpa;
    uint32_t ours;
    if (h < 0 || frame == NULL || len < 42u) {
        return;
    }
    if (pm_ip_read_be16(frame + 14) != 1u || pm_ip_read_be16(frame + 16) != 0x0800u) {
        return;
    }
    if (frame[18] != 6u || frame[19] != 4u || pm_ip_read_be16(frame + 20) != 1u) {
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
    memset(out, 0, sizeof(out));
    memset(mac, 0, sizeof(mac));
    pm_metal_drivers_net_mac(h, mac);
    memcpy(out, frame + 6, 6);
    memcpy(out + 6, mac, 6);
    out[12] = 0x08;
    out[13] = 0x06;
    pm_ip_write_be16(out + 14, 1);
    pm_ip_write_be16(out + 16, 0x0800);
    out[18] = 6;
    out[19] = 4;
    pm_ip_write_be16(out + 20, 2);
    memcpy(out + 22, mac, 6);
    pm_ip_write_be32(out + 28, ours);
    memcpy(out + 32, frame + 22, 6);
    memcpy(out + 38, frame + 28, 4);
    (void)pm_metal_drivers_net_tx(h, out, (uint16_t)sizeof(out));
}

void pm_ip_arp_announce(int32_t h) {
    uint8_t out[60];
    uint8_t mac[6];
    uint32_t ours;
    if (h < 0) {
        return;
    }
    ours = pm_ip_l2_addr_of(h);
    if (ours == 0) {
        return;
    }
    memset(out, 0, sizeof(out));
    memset(mac, 0, sizeof(mac));
    pm_metal_drivers_net_mac(h, mac);
    memset(out, 0xff, 6);
    memcpy(out + 6, mac, 6);
    out[12] = 0x08;
    out[13] = 0x06;
    pm_ip_write_be16(out + 14, 1);
    pm_ip_write_be16(out + 16, 0x0800);
    out[18] = 6;
    out[19] = 4;
    pm_ip_write_be16(out + 20, 1);
    memcpy(out + 22, mac, 6);
    pm_ip_write_be32(out + 28, ours);
    pm_ip_write_be32(out + 38, ours);
    (void)pm_metal_drivers_net_tx(h, out, (uint16_t)sizeof(out));
}
