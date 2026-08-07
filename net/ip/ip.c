#include "pymergetic/metal/net/ip.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/dev/net.h"

static int32_t g_ready;
static uint32_t g_addr; /* host order */
static uint32_t g_mask;
static uint32_t g_gw;
static uint8_t g_mac[6];

static uint16_t htons16(uint16_t v)
{
    return (uint16_t)((v << 8) | (v >> 8));
}

static uint32_t htons32(uint32_t v)
{
    return ((v & 0x000000ffu) << 24) | ((v & 0x0000ff00u) << 8) |
           ((v & 0x00ff0000u) >> 8) | ((v & 0xff000000u) >> 24);
}

static uint16_t checksum(const uint8_t *data, uint32_t len)
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

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
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
    frame[21] = 0x02; /* reply */
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
    uint32_t i;

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
    /* eth swap */
    memcpy(out + 0, frame + 6, 6);
    memcpy(out + 6, g_mac, 6);
    /* IP swap + TTL */
    memcpy(out + 14 + 12, frame + 14 + 16, 4); /* dst = old src */
    put_u32(out + 14 + 16, g_addr);            /* src = us */
    out[14 + 8] = 64;
    put_u16(out + 14 + 10, 0);
    put_u16(out + 14 + 10, checksum(out + 14, ihl));
    /* ICMP echo reply */
    out[icmp_off] = 0; /* type echo reply */
    put_u16(out + icmp_off + 2, 0);
    put_u16(out + icmp_off + 2, checksum(out + icmp_off, icmp_len));

    (void)i;
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
        /* ARP */
        uint16_t oper;
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
        proto = frame[14 + 9];
        dst = get_u32(frame + 14 + 16);
        if (dst != g_addr) {
            return;
        }
        if (proto == 1u && frame[14 + ihl] == 8u) {
            /* ICMP echo request */
            (void)tx_icmp_echo_reply(frame, len);
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
    /* API takes host-order-looking constants; store as host order ints. */
    g_addr = addr_be;
    g_mask = mask_be;
    g_gw = gw_be;
    (void)htons16;
    (void)htons32;
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
    frame[21] = 0x01; /* request (gratuitous) */
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
