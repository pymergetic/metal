#include "pymergetic/metal/net/ip/udp.h"
#include "pymergetic/metal/net/ip/internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define UDP_RX_SLOTS 4u
#define UDP_RX_BUF   512u

typedef struct {
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t len;
    uint8_t data[UDP_RX_BUF];
} udp_rx_pkt_t;

static uint16_t g_bind_port;
static int32_t g_bound;
static udp_rx_pkt_t g_rx[UDP_RX_SLOTS];
static uint32_t g_rx_head;
static uint32_t g_rx_tail;

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint16_t put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
    return v;
}

static void udp_on_rx(const uint8_t *ip_pkt, uint32_t ip_len, uint32_t ihl)
{
    uint32_t udp_off;
    uint32_t udp_len;
    uint16_t dst_port;
    uint32_t idx;
    udp_rx_pkt_t *slot;

    if (!g_bound || ip_pkt == NULL || ip_len < ihl + 8u) {
        return;
    }
    udp_off = ihl;
    udp_len = ip_len - ihl;
    if (udp_len < 8u) {
        return;
    }
    dst_port = get_u16(ip_pkt + udp_off + 2);
    if (dst_port != g_bind_port) {
        return;
    }

    idx = (g_rx_head + 1u) % UDP_RX_SLOTS;
    if (idx == g_rx_tail) {
        return;
    }
    slot = &g_rx[g_rx_head];
    slot->src_ip = ((uint32_t)ip_pkt[12] << 24) | ((uint32_t)ip_pkt[13] << 16) |
                   ((uint32_t)ip_pkt[14] << 8) | ip_pkt[15];
    slot->src_port = get_u16(ip_pkt + udp_off);
    slot->dst_port = dst_port;
    slot->len = (uint16_t)(udp_len - 8u);
    if (slot->len > UDP_RX_BUF) {
        slot->len = UDP_RX_BUF;
    }
    memcpy(slot->data, ip_pkt + udp_off + 8, slot->len);
    g_rx_head = idx;
}

static void udp_register_once(void)
{
    static int32_t registered;

    if (!registered) {
        pm_metal_net_ip_register_udp_rx(udp_on_rx);
        registered = 1;
    }
}

int32_t pm_metal_net_ip_udp_bind(uint16_t local_port)
{
    udp_register_once();
    g_bind_port = local_port;
    g_bound = 1;
    g_rx_head = 0;
    g_rx_tail = 0;
    return 0;
}

int32_t pm_metal_net_ip_udp_sendto(uint32_t dst_ip, uint16_t dst_port,
                            const void *data, uint32_t len)
{
    uint8_t seg[1500];
    uint32_t seg_len;

    if (data == NULL || len == 0u || len + 8u > sizeof(seg)) {
        return -1;
    }
    udp_register_once();
    seg_len = 8u + len;
    put_u16(seg + 0, g_bound ? g_bind_port : 0);
    put_u16(seg + 2, dst_port);
    put_u16(seg + 4, (uint16_t)seg_len);
    put_u16(seg + 6, 0);
    memcpy(seg + 8, data, len);
    put_u16(seg + 6, pm_metal_net_ip_l4_checksum(pm_metal_net_ip_addr_host(), dst_ip, 17, seg, seg_len));
    return pm_metal_net_ip_tx_l4(dst_ip, 17, seg, seg_len);
}

int32_t pm_metal_net_ip_udp_recv(uint32_t *src_ip, uint16_t *src_port,
                          void *buf, uint32_t cap, uint32_t *len_out)
{
    udp_rx_pkt_t *slot;
    uint32_t n;

    if (!g_bound || buf == NULL || cap == 0u) {
        return -1;
    }
    if (g_rx_tail == g_rx_head) {
        return 0;
    }
    slot = &g_rx[g_rx_tail];
    n = slot->len;
    if (n > cap) {
        n = cap;
    }
    memcpy(buf, slot->data, n);
    if (src_ip != NULL) {
        *src_ip = slot->src_ip;
    }
    if (src_port != NULL) {
        *src_port = slot->src_port;
    }
    if (len_out != NULL) {
        *len_out = n;
    }
    g_rx_tail = (g_rx_tail + 1u) % UDP_RX_SLOTS;
    return 1;
}
