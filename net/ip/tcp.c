#include "pymergetic/metal/net/tcp.h"
#include "pymergetic/metal/net/ip_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TCP_CLOSED      0u
#define TCP_LISTEN      1u
#define TCP_SYN_RECV    2u
#define TCP_ESTABLISHED 3u

#define TCP_FLAG_FIN 0x01u
#define TCP_FLAG_SYN 0x02u
#define TCP_FLAG_RST 0x04u
#define TCP_FLAG_PSH 0x08u
#define TCP_FLAG_ACK 0x10u

#define TCP_RX_CAP 512u

typedef struct {
    uint8_t state;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint32_t snd_isn;
    uint32_t rcv_nxt;
    uint32_t snd_nxt;
    int32_t syn_acked;
    int32_t established;
    uint8_t rx[TCP_RX_CAP];
    uint32_t rx_len;
} tcp_sock_t;

static tcp_sock_t g_sock;
static uint32_t g_isn_seq = 0x12345678u;

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
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

static int32_t tcp_tx(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                      uint32_t seq, uint32_t ack, uint8_t flags,
                      const uint8_t *payload, uint32_t payload_len)
{
    uint8_t seg[1500];
    uint32_t seg_len;

    if (payload_len + 20u > sizeof(seg)) {
        return -1;
    }
    seg_len = 20u + payload_len;
    put_u16(seg + 0, src_port);
    put_u16(seg + 2, dst_port);
    put_u32(seg + 4, seq);
    put_u32(seg + 8, ack);
    seg[12] = 0x50;
    seg[13] = flags;
    put_u16(seg + 14, 8192);
    put_u16(seg + 16, 0);
    put_u16(seg + 18, 0);
    if (payload_len > 0u && payload != NULL) {
        memcpy(seg + 20, payload, payload_len);
    }
    put_u16(seg + 16, pm_metal_ip_l4_checksum(pm_metal_ip_addr_host(), dst_ip, 6, seg, seg_len));
    return pm_metal_ip_tx_l4(dst_ip, 6, seg, seg_len);
}

static void tcp_queue_rx(const uint8_t *data, uint32_t len)
{
    uint32_t space;
    uint32_t take;

    if (data == NULL || len == 0u) {
        return;
    }
    space = TCP_RX_CAP - g_sock.rx_len;
    take = len < space ? len : space;
    if (take == 0u) {
        return;
    }
    memcpy(g_sock.rx + g_sock.rx_len, data, take);
    g_sock.rx_len += take;
}

static void tcp_on_rx(const uint8_t *ip_pkt, uint32_t ip_len, uint32_t ihl)
{
    uint32_t tcp_off;
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t flags;
    uint32_t tcp_hdr_len;
    uint32_t payload_len;

    if (g_sock.state == TCP_CLOSED || ip_pkt == NULL || ip_len < ihl + 20u) {
        return;
    }

    tcp_off = ihl;
    src_ip = get_u32(ip_pkt + 12);
    src_port = get_u16(ip_pkt + tcp_off);
    dst_port = get_u16(ip_pkt + tcp_off + 2);
    seq = get_u32(ip_pkt + tcp_off + 4);
    ack = get_u32(ip_pkt + tcp_off + 8);
    flags = ip_pkt[tcp_off + 13];
    tcp_hdr_len = (uint32_t)((ip_pkt[tcp_off + 12] >> 4) & 0x0fu) * 4u;
    if (tcp_hdr_len < 20u || ihl + tcp_hdr_len > ip_len) {
        return;
    }
    if (dst_port != g_sock.local_port) {
        return;
    }

    if (pm_metal_ip_arp_lookup(src_ip) == NULL) {
        (void)pm_metal_ip_arp_resolve(src_ip);
    }

    if (g_sock.state == TCP_LISTEN && (flags & TCP_FLAG_SYN) != 0u && (flags & TCP_FLAG_ACK) == 0u) {
        g_sock.state = TCP_SYN_RECV;
        g_sock.remote_ip = src_ip;
        g_sock.remote_port = src_port;
        g_sock.rcv_nxt = seq + 1u;
        g_sock.snd_isn = g_isn_seq;
        g_sock.snd_nxt = g_isn_seq + 1u;
        if (tcp_tx(src_ip, g_sock.local_port, src_port, g_sock.snd_isn, g_sock.rcv_nxt,
                   (uint8_t)(TCP_FLAG_SYN | TCP_FLAG_ACK), NULL, 0) == 0) {
            g_sock.syn_acked = 1;
        }
        return;
    }

    if (g_sock.state == TCP_SYN_RECV && (flags & TCP_FLAG_ACK) != 0u) {
        if (ack == g_sock.snd_nxt && src_ip == g_sock.remote_ip && src_port == g_sock.remote_port) {
            g_sock.state = TCP_ESTABLISHED;
            g_sock.established = 1;
        }
        return;
    }

    if (g_sock.state == TCP_ESTABLISHED && src_ip == g_sock.remote_ip &&
        src_port == g_sock.remote_port) {
        payload_len = ip_len - (ihl + tcp_hdr_len);
        if (payload_len > 0u && seq == g_sock.rcv_nxt) {
            tcp_queue_rx(ip_pkt + ihl + tcp_hdr_len, payload_len);
            g_sock.rcv_nxt += payload_len;
            (void)tcp_tx(src_ip, g_sock.local_port, src_port, g_sock.snd_nxt, g_sock.rcv_nxt,
                         TCP_FLAG_ACK, NULL, 0);
        }
        (void)ack;
        (void)flags;
        return;
    }
}

static void tcp_register_once(void)
{
    static int32_t registered;

    if (!registered) {
        pm_metal_ip_register_tcp_rx(tcp_on_rx);
        registered = 1;
    }
}

int32_t pm_metal_tcp_listen(uint16_t local_port)
{
    tcp_register_once();
    memset(&g_sock, 0, sizeof(g_sock));
    g_sock.state = TCP_LISTEN;
    g_sock.local_port = local_port;
    return 0;
}

int32_t pm_metal_tcp_established(void)
{
    return g_sock.established ? 1 : 0;
}

int32_t pm_metal_tcp_send(const void *data, uint32_t len)
{
    int32_t rc;

    if (!g_sock.established || data == NULL || len == 0u) {
        return -1;
    }
    rc = tcp_tx(g_sock.remote_ip, g_sock.local_port, g_sock.remote_port, g_sock.snd_nxt,
                g_sock.rcv_nxt, (uint8_t)(TCP_FLAG_PSH | TCP_FLAG_ACK), (const uint8_t *)data, len);
    if (rc == 0) {
        g_sock.snd_nxt += len;
    }
    return rc;
}

int32_t pm_metal_tcp_recv(uint8_t *buf, uint32_t cap, uint32_t *len_out)
{
    uint32_t n;

    if (buf == NULL || len_out == NULL || cap == 0u) {
        return -1;
    }
    if (g_sock.rx_len == 0u) {
        *len_out = 0;
        return 0;
    }
    n = g_sock.rx_len < cap ? g_sock.rx_len : cap;
    memcpy(buf, g_sock.rx, n);
    if (n < g_sock.rx_len) {
        memmove(g_sock.rx, g_sock.rx + n, g_sock.rx_len - n);
    }
    g_sock.rx_len -= n;
    *len_out = n;
    return 1;
}

static void tcp_inject(const uint8_t *ip_pkt, uint32_t ip_len)
{
    tcp_on_rx(ip_pkt, ip_len, 20);
}

int32_t pm_metal_tcp_smoke_syn_ack(void)
{
    uint8_t ip_pkt[40];
    const uint8_t peer_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    uint32_t peer_ip = 0x0a000202u;

    if (g_sock.state != TCP_LISTEN) {
        return -1;
    }

    pm_metal_ip_arp_cache_put(peer_ip, peer_mac);

    memset(ip_pkt, 0, sizeof(ip_pkt));
    ip_pkt[0] = 0x45;
    ip_pkt[9] = 6;
    put_u16(ip_pkt + 2, 40);
    put_u32(ip_pkt + 12, peer_ip);
    put_u32(ip_pkt + 16, pm_metal_ip_addr_host());
    put_u16(ip_pkt + 20, 40000);
    put_u16(ip_pkt + 22, g_sock.local_port);
    put_u32(ip_pkt + 24, 0xabcdef01u);
    ip_pkt[33] = TCP_FLAG_SYN;
    ip_pkt[32] = 0x50;

    tcp_inject(ip_pkt, 40);

    if (!g_sock.syn_acked || g_sock.state != TCP_SYN_RECV) {
        return -1;
    }

    memset(ip_pkt + 20, 0, 20);
    put_u16(ip_pkt + 20, 40000);
    put_u16(ip_pkt + 22, g_sock.local_port);
    put_u32(ip_pkt + 24, 0xabcdef02u);
    put_u32(ip_pkt + 28, g_sock.snd_nxt);
    ip_pkt[33] = TCP_FLAG_ACK;
    ip_pkt[32] = 0x50;
    tcp_inject(ip_pkt, 40);

    return g_sock.established ? 0 : -1;
}

int32_t pm_metal_tcp_smoke_inject_payload(const void *data, uint32_t len)
{
    uint8_t ip_pkt[20 + 20 + 512];
    uint32_t total;
    uint32_t peer_ip = 0x0a000202u;

    if (!g_sock.established || data == NULL || len == 0u || len > 512u) {
        return -1;
    }
    total = 40u + len;
    memset(ip_pkt, 0, total);
    ip_pkt[0] = 0x45;
    ip_pkt[9] = 6;
    put_u16(ip_pkt + 2, (uint16_t)total);
    put_u32(ip_pkt + 12, peer_ip);
    put_u32(ip_pkt + 16, pm_metal_ip_addr_host());
    put_u16(ip_pkt + 20, 40000);
    put_u16(ip_pkt + 22, g_sock.local_port);
    put_u32(ip_pkt + 24, g_sock.rcv_nxt);
    put_u32(ip_pkt + 28, g_sock.snd_nxt);
    ip_pkt[32] = 0x50;
    ip_pkt[33] = (uint8_t)(TCP_FLAG_PSH | TCP_FLAG_ACK);
    memcpy(ip_pkt + 40, data, len);
    tcp_inject(ip_pkt, total);
    return 0;
}
