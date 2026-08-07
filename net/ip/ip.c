#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/ip_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/dev/net.h"

#define ARP_CACHE_SIZE 16u
#define IP_PROTO_ICMP 1u
#define IP_PROTO_TCP  6u
#define IP_PROTO_UDP  17u

typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    uint8_t valid;
} arp_entry_t;

static int32_t g_ready;
static uint32_t g_addr;
static uint32_t g_mask;
static uint32_t g_gw;
static uint32_t g_dns;
static uint8_t g_mac[6];
static arp_entry_t g_arp_cache[ARP_CACHE_SIZE];
static pm_metal_ip_l4_rx_fn g_udp_rx;
static pm_metal_ip_l4_rx_fn g_tcp_rx;
static uint16_t g_ping_id;
static uint16_t g_ping_seq;
static uint32_t g_ping_replies;

static uint16_t put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
    return v;
}

static uint32_t put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
    return v;
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

uint16_t pm_metal_ip_checksum(const uint8_t *data, uint32_t len)
{
    uint32_t sum = 0;
    uint32_t i;

    for (i = 0; i + 1u < len; i += 2u) {
        sum += ((uint32_t)data[i] << 8) | (uint32_t)data[i + 1u];
    }
    if (i < len) {
        sum += (uint32_t)data[i] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

uint16_t pm_metal_ip_l4_checksum(uint32_t src_ip, uint32_t dst_ip, uint8_t proto,
                                 const uint8_t *seg, uint32_t seg_len)
{
    uint8_t pseudo[12];
    uint32_t sum = 0;
    uint32_t i;

    put_u32(pseudo + 0, src_ip);
    put_u32(pseudo + 4, dst_ip);
    pseudo[8] = 0;
    pseudo[9] = proto;
    put_u16(pseudo + 10, (uint16_t)seg_len);

    for (i = 0; i + 1u < 12u; i += 2u) {
        sum += ((uint32_t)pseudo[i] << 8) | (uint32_t)pseudo[i + 1u];
    }
    for (i = 0; i + 1u < seg_len; i += 2u) {
        sum += ((uint32_t)seg[i] << 8) | (uint32_t)seg[i + 1u];
    }
    if (i < seg_len) {
        sum += (uint32_t)seg[i] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

void pm_metal_ip_register_udp_rx(pm_metal_ip_l4_rx_fn fn)
{
    g_udp_rx = fn;
}

void pm_metal_ip_register_tcp_rx(pm_metal_ip_l4_rx_fn fn)
{
    g_tcp_rx = fn;
}

const uint8_t *pm_metal_ip_mac(void)
{
    return g_mac;
}

uint32_t pm_metal_ip_addr_host(void)
{
    return g_addr;
}

void pm_metal_ip_arp_cache_put(uint32_t ip, const uint8_t mac[6])
{
    uint32_t i;
    uint32_t slot = ARP_CACHE_SIZE;

    if (mac == NULL || ip == 0u) {
        return;
    }
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
            slot = i;
            break;
        }
        if (!g_arp_cache[i].valid && slot == ARP_CACHE_SIZE) {
            slot = i;
        }
    }
    if (slot == ARP_CACHE_SIZE) {
        /* Evict a non-gateway entry; never drop the default route L2 mapping. */
        slot = 0;
        for (i = 0; i < ARP_CACHE_SIZE; i++) {
            if (!g_arp_cache[i].valid || g_arp_cache[i].ip != g_gw) {
                slot = i;
                break;
            }
        }
    }
    g_arp_cache[slot].ip = ip;
    memcpy(g_arp_cache[slot].mac, mac, 6);
    g_arp_cache[slot].valid = 1;
}

const uint8_t *pm_metal_ip_arp_lookup(uint32_t ip_host)
{
    uint32_t i;

    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip_host) {
            return g_arp_cache[i].mac;
        }
    }
    return NULL;
}

static int32_t tx_arp_request(uint32_t target_ip)
{
    uint8_t frame[64];

    if (!g_ready) {
        return -1;
    }
    memset(frame, 0, sizeof(frame));
    memset(frame + 0, 0xff, 6);
    memcpy(frame + 6, g_mac, 6);
    frame[12] = 0x08;
    frame[13] = 0x06;
    frame[14] = 0x00;
    frame[15] = 0x01;
    frame[16] = 0x08;
    frame[17] = 0x00;
    frame[18] = 0x06;
    frame[19] = 0x04;
    frame[20] = 0x00;
    frame[21] = 0x01;
    memcpy(frame + 22, g_mac, 6);
    put_u32(frame + 28, g_addr);
    memset(frame + 32, 0x00, 6);
    put_u32(frame + 38, target_ip);
    return pm_metal_dev_net_virtio_tx(frame, 42) == 0 ? 0 : -1;
}

int32_t pm_metal_ip_arp_resolve(uint32_t ip_host)
{
    if (!g_ready) {
        return -1;
    }
    if (pm_metal_ip_arp_lookup(ip_host) != NULL) {
        return 1;
    }
    return tx_arp_request(ip_host) == 0 ? 0 : -1;
}

static uint32_t ip_nexthop(uint32_t dst_ip)
{
    if (dst_ip == 0xffffffffu) {
        return dst_ip;
    }
    if (g_addr == 0u) {
        return dst_ip;
    }
    if ((dst_ip & g_mask) == (g_addr & g_mask)) {
        return dst_ip;
    }
    return g_gw;
}

int32_t pm_metal_ip_tx_l4(uint32_t dst_ip_host, uint8_t proto,
                           const uint8_t *l4, uint32_t l4_len)
{
    uint8_t frame[1518];
    uint32_t ip_len;
    uint32_t frame_len;
    const uint8_t *dst_mac;
    uint32_t nh;
    static const uint8_t bcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    if (!g_ready || l4 == NULL || l4_len == 0u || l4_len + 20u > 1500u) {
        return -1;
    }

    if (dst_ip_host == 0xffffffffu) {
        dst_mac = bcast_mac;
    } else {
        nh = ip_nexthop(dst_ip_host);
        dst_mac = pm_metal_ip_arp_lookup(nh);
        if (dst_mac == NULL) {
            (void)tx_arp_request(nh);
            return -2;
        }
    }

    ip_len = 20u + l4_len;
    frame_len = 14u + ip_len;
    if (frame_len > sizeof(frame)) {
        return -1;
    }

    memcpy(frame + 0, dst_mac, 6);
    memcpy(frame + 6, g_mac, 6);
    frame[12] = 0x08;
    frame[13] = 0x00;
    frame[14] = 0x45;
    frame[15] = 0x00;
    put_u16(frame + 16, (uint16_t)ip_len);
    put_u16(frame + 18, 0);
    put_u16(frame + 20, 0);
    frame[22] = 64;
    frame[23] = proto;
    put_u16(frame + 24, 0);
    put_u32(frame + 26, g_addr);
    put_u32(frame + 30, dst_ip_host);
    put_u16(frame + 24, 0);
    put_u16(frame + 24, pm_metal_ip_checksum(frame + 14, 20));
    memcpy(frame + 34, l4, l4_len);

    {
        int32_t tries;
        for (tries = 0; tries < 8; tries++) {
            if (pm_metal_dev_net_virtio_tx(frame, frame_len) == 0) {
                return 0;
            }
            pm_metal_dev_net_virtio_poll(NULL, NULL);
            (void)pm_metal_dev_net_virtio_reap_tx();
        }
    }
    return -1;
}

static int32_t tx_arp_reply(const uint8_t *req_frame)
{
    uint8_t frame[64];
    const uint8_t *sha = req_frame + 22;
    const uint8_t *spa = req_frame + 28;

    memset(frame, 0, sizeof(frame));
    memcpy(frame + 0, sha, 6);
    memcpy(frame + 6, g_mac, 6);
    frame[12] = 0x08;
    frame[13] = 0x06;
    frame[14] = 0x00;
    frame[15] = 0x01;
    frame[16] = 0x08;
    frame[17] = 0x00;
    frame[18] = 0x06;
    frame[19] = 0x04;
    frame[20] = 0x00;
    frame[21] = 0x02;
    memcpy(frame + 22, g_mac, 6);
    put_u32(frame + 28, g_addr);
    memcpy(frame + 32, sha, 6);
    memcpy(frame + 38, spa, 4);
    return pm_metal_dev_net_virtio_tx(frame, 42) == 0 ? 0 : -1;
}

static int32_t tx_icmp_echo_reply(const uint8_t *frame, uint32_t len)
{
    uint8_t out[1518];
    uint32_t ihl;
    uint32_t ip_len;
    uint32_t icmp_off;
    uint32_t icmp_len;

    if (len < 34u) {
        return -1;
    }
    ihl = (uint32_t)(frame[14] & 0x0fu) * 4u;
    if (ihl < 20u || 14u + ihl + 8u > len) {
        return -1;
    }
    ip_len = get_u16(frame + 16);
    if (ip_len < ihl + 8u || 14u + ip_len > len) {
        ip_len = len - 14u;
    }
    icmp_off = 14u + ihl;
    icmp_len = ip_len - ihl;
    if (icmp_len < 8u || icmp_off + icmp_len > sizeof(out)) {
        return -1;
    }

    memcpy(out, frame, 14u + ip_len);
    memcpy(out + 0, frame + 6, 6);
    memcpy(out + 6, g_mac, 6);
    memcpy(out + 14 + 12, frame + 14 + 16, 4);
    put_u32(out + 14 + 16, g_addr);
    out[14 + 8] = 64;
    put_u16(out + 14 + 10, 0);
    put_u16(out + 14 + 10, pm_metal_ip_checksum(out + 14, ihl));
    out[icmp_off] = 0;
    put_u16(out + icmp_off + 2, 0);
    put_u16(out + icmp_off + 2, pm_metal_ip_checksum(out + icmp_off, icmp_len));

    return pm_metal_dev_net_virtio_tx(out, 14u + ip_len) == 0 ? 0 : -1;
}

static void on_frame(void *ctx, const uint8_t *frame, uint32_t len)
{
    uint16_t ethertype;

    (void)ctx;
    if (!g_ready || frame == NULL || len < 14u) {
        return;
    }
    ethertype = get_u16(frame + 12);

    if (ethertype == 0x0806u) {
        uint16_t oper;
        uint32_t spa;
        uint32_t tpa;
        if (len < 42u) {
            return;
        }
        if (get_u16(frame + 14) != 0x0001u || get_u16(frame + 16) != 0x0800u) {
            return;
        }
        if (frame[18] != 6u || frame[19] != 4u) {
            return;
        }
        oper = get_u16(frame + 20);
        spa = get_u32(frame + 28);
        if (oper == 2u) {
            pm_metal_ip_arp_cache_put(spa, frame + 22);
        }
        tpa = get_u32(frame + 38);
        if (oper == 1u && tpa == g_addr) {
            (void)tx_arp_reply(frame);
        }
        return;
    }

    if (ethertype == 0x0800u) {
        uint32_t ihl;
        uint8_t proto;
        uint32_t dst;
        uint32_t src;
        uint32_t ip_len;

        if (len < 34u) {
            return;
        }
        if ((frame[14] >> 4) != 4u) {
            return;
        }
        ihl = (uint32_t)(frame[14] & 0x0fu) * 4u;
        if (ihl < 20u || 14u + ihl + 8u > len) {
            return;
        }
        ip_len = get_u16(frame + 16);
        if (ip_len < ihl || 14u + ip_len > len) {
            ip_len = len - 14u;
        }
        proto = frame[14 + 9];
        src = get_u32(frame + 14 + 12);
        dst = get_u32(frame + 14 + 16);
        /* Learn L2 mapping from the frame that just arrived. */
        pm_metal_ip_arp_cache_put(src, frame + 6);
        if (dst != g_addr && dst != 0xffffffffu && g_addr != 0u) {
            return;
        }
        if (proto == IP_PROTO_ICMP) {
            uint8_t icmp_type = frame[14 + ihl];
            if (icmp_type == 8u) {
                (void)tx_icmp_echo_reply(frame, len);
                return;
            }
            if (icmp_type == 0u && ip_len >= ihl + 8u) {
                uint16_t id = get_u16(frame + 14 + ihl + 4);
                uint16_t seq = get_u16(frame + 14 + ihl + 6);
                if (id == g_ping_id && seq == g_ping_seq) {
                    g_ping_replies++;
                }
            }
            return;
        }
        if (proto == IP_PROTO_UDP && g_udp_rx != NULL) {
            g_udp_rx(frame + 14, ip_len, ihl);
            return;
        }
        if (proto == IP_PROTO_TCP && g_tcp_rx != NULL) {
            g_tcp_rx(frame + 14, ip_len, ihl);
        }
    }
}

int32_t pm_metal_ip_init(uint32_t addr_be, uint32_t mask_be, uint32_t gw_be)
{
    const uint8_t *mac;

    if (!pm_metal_dev_net_virtio_ready()) {
        return -1;
    }
    mac = pm_metal_dev_net_virtio_mac();
    if (mac == NULL) {
        return -1;
    }
    memcpy(g_mac, mac, 6);
    g_addr = addr_be;
    g_mask = mask_be;
    g_gw = gw_be;
    g_dns = PM_METAL_IP_DEFAULT_DNS;
    memset(g_arp_cache, 0, sizeof(g_arp_cache));
    g_udp_rx = NULL;
    g_tcp_rx = NULL;
    g_ping_id = 0;
    g_ping_seq = 0;
    g_ping_replies = 0;
    g_ready = 1;
    return 0;
}

int32_t pm_metal_ip_ready(void)
{
    return g_ready;
}

uint32_t pm_metal_ip_addr(void)
{
    return g_addr;
}

uint32_t pm_metal_ip_gw(void)
{
    return g_gw;
}

uint32_t pm_metal_ip_mask(void)
{
    return g_mask;
}

int32_t pm_metal_ip_set_addrs(uint32_t addr, uint32_t mask, uint32_t gw)
{
    if (!g_ready) {
        return -1;
    }
    g_addr = addr;
    g_mask = mask;
    g_gw = gw;
    return 0;
}

int32_t pm_metal_ip_set_dns(uint32_t dns)
{
    if (!g_ready) {
        return -1;
    }
    g_dns = dns != 0u ? dns : PM_METAL_IP_DEFAULT_DNS;
    return 0;
}

uint32_t pm_metal_ip_dns(void)
{
    return g_dns != 0u ? g_dns : PM_METAL_IP_DEFAULT_DNS;
}

int32_t pm_metal_ip_announce(void)
{
    uint8_t frame[64];

    if (!g_ready) {
        return -1;
    }
    memset(frame, 0, sizeof(frame));
    memset(frame + 0, 0xff, 6);
    memcpy(frame + 6, g_mac, 6);
    frame[12] = 0x08;
    frame[13] = 0x06;
    frame[14] = 0x00;
    frame[15] = 0x01;
    frame[16] = 0x08;
    frame[17] = 0x00;
    frame[18] = 0x06;
    frame[19] = 0x04;
    frame[20] = 0x00;
    frame[21] = 0x01;
    memcpy(frame + 22, g_mac, 6);
    put_u32(frame + 28, g_addr);
    memset(frame + 32, 0x00, 6);
    put_u32(frame + 38, g_addr);
    if (pm_metal_dev_net_virtio_tx(frame, 42) != 0) {
        return -1;
    }
    return 0;
}

void pm_metal_ip_poll(void)
{
    if (!g_ready) {
        return;
    }
    pm_metal_dev_net_virtio_poll(on_frame, NULL);
    (void)pm_metal_dev_net_virtio_reap_tx();
}

int32_t pm_metal_ip_ping(uint32_t dst_ip, uint16_t id, uint16_t seq)
{
    uint8_t icmp[16];

    if (!g_ready) {
        return -1;
    }
    g_ping_id = id;
    g_ping_seq = seq;
    icmp[0] = 8; /* echo request */
    icmp[1] = 0;
    put_u16(icmp + 2, 0);
    put_u16(icmp + 4, id);
    put_u16(icmp + 6, seq);
    memset(icmp + 8, 0x5a, 8);
    put_u16(icmp + 2, pm_metal_ip_checksum(icmp, sizeof(icmp)));
    return pm_metal_ip_tx_l4(dst_ip, IP_PROTO_ICMP, icmp, sizeof(icmp));
}

uint32_t pm_metal_ip_ping_replies(void)
{
    return g_ping_replies;
}
