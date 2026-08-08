#include "packet.h"

#include <string.h>

#include "pymergetic/metal/net/ip/tcp.h"

static uint8_t g_acc[PM_METAL_SSH_PKT_ACC];
static uint32_t g_acc_len;

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
        | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void pm_metal_net_ssh_pkt_reset(void)
{
    g_acc_len = 0;
}

int32_t pm_metal_net_ssh_pkt_push(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0u) {
        return 0;
    }
    if (g_acc_len + len > sizeof(g_acc)) {
        pm_metal_net_ssh_pkt_reset();
        return -1;
    }
    memcpy(g_acc + g_acc_len, data, len);
    g_acc_len += len;
    return 0;
}

int32_t pm_metal_net_ssh_pkt_send(const uint8_t *payload, uint32_t payload_len)
{
    uint8_t pkt[PM_METAL_SSH_PKT_ACC];
    uint32_t pad;
    uint32_t pkt_len;
    uint32_t i;

    if (payload == NULL || payload_len == 0u || payload_len > 3500u) {
        return -1;
    }
    /*
     * RFC4253: len(packet_length || padding_length || payload || padding)
     * must be a multiple of 8; padding_length >= 4.
     */
    pad = (8u - ((payload_len + 5u) % 8u)) % 8u;
    if (pad < 4u) {
        pad += 8u;
    }
    pkt_len = 1u + payload_len + pad;
    if (5u + pkt_len > sizeof(pkt)) {
        return -1;
    }
    put_u32(pkt, pkt_len);
    pkt[4] = (uint8_t)pad;
    memcpy(pkt + 5, payload, payload_len);
    for (i = 0; i < pad; i++) {
        pkt[5u + payload_len + i] = (uint8_t)(0xa5u ^ (uint8_t)i);
    }
    return pm_metal_net_ip_tcp_send(pkt, 4u + pkt_len);
}

int32_t pm_metal_net_ssh_pkt_recv(uint8_t *payload, uint32_t cap, uint32_t *len_out)
{
    uint8_t chunk[256];
    uint32_t n = 0;
    uint32_t pkt_len;
    uint32_t need;
    uint8_t pad;
    uint32_t plen;
    int32_t rc;

    if (payload == NULL || len_out == NULL) {
        return -1;
    }
    *len_out = 0;
    rc = pm_metal_net_ip_tcp_recv(chunk, sizeof(chunk), &n);
    if (rc == 1 && n > 0u) {
        if (g_acc_len + n > sizeof(g_acc)) {
            pm_metal_net_ssh_pkt_reset();
            return -1;
        }
        memcpy(g_acc + g_acc_len, chunk, n);
        g_acc_len += n;
    }
    if (g_acc_len < 4u) {
        return 0;
    }
    pkt_len = get_u32(g_acc);
    if (pkt_len < 5u || pkt_len > sizeof(g_acc) - 4u) {
        pm_metal_net_ssh_pkt_reset();
        return -1;
    }
    need = 4u + pkt_len;
    if (g_acc_len < need) {
        return 0;
    }
    pad = g_acc[4];
    if (pad < 4u || (uint32_t)pad + 1u >= pkt_len) {
        pm_metal_net_ssh_pkt_reset();
        return -1;
    }
    plen = pkt_len - 1u - (uint32_t)pad;
    if (plen > cap) {
        pm_metal_net_ssh_pkt_reset();
        return -1;
    }
    memcpy(payload, g_acc + 5, plen);
    *len_out = plen;
    if (g_acc_len > need) {
        memmove(g_acc, g_acc + need, g_acc_len - need);
        g_acc_len -= need;
    } else {
        g_acc_len = 0;
    }
    return 1;
}
