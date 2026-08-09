#include "packet.h"

#include <string.h>

#include "monocypher.h"
#include "pymergetic/metal/net/ip/sock.h"

static uint8_t g_acc[PM_METAL_SSH_PKT_ACC];
static uint32_t g_acc_len;
static int g_encrypted;
static uint8_t g_key_c2s[64];
static uint8_t g_key_s2c[64];
static uint32_t g_seq_out; /* server→client */
static uint32_t g_seq_in;  /* client→server */
static pm_metal_net_ip_sock_h g_sock;

void pm_metal_net_ssh_pkt_bind_sock(uint32_t sock_h)
{
    g_sock = (pm_metal_net_ip_sock_h)sock_h;
}

static int32_t sock_send(const void *data, uint32_t len)
{
    if (g_sock == PM_METAL_NET_IP_SOCK_INVALID || data == NULL || len == 0u) {
        return -1;
    }
    return pm_metal_net_ip_send(g_sock, data, len) == len ? 0 : -1;
}

static int32_t sock_recv(uint8_t *buf, uint32_t cap, uint32_t *n_out)
{
    uint32_t n;

    if (n_out == NULL || buf == NULL || cap == 0u) {
        return -1;
    }
    *n_out = 0;
    if (g_sock == PM_METAL_NET_IP_SOCK_INVALID) {
        return -1;
    }
    n = pm_metal_net_ip_try_recv(g_sock, buf, cap);
    if (n == 0u) {
        return 0;
    }
    if (n == (uint32_t)-1) {
        return -1;
    }
    *n_out = n;
    return 1;
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8)
           | (uint32_t)p[3];
}

static void put_u64_be(uint8_t *p, uint64_t v)
{
    int i;
    for (i = 7; i >= 0; i--) {
        p[i] = (uint8_t)(v & 0xffu);
        v >>= 8;
    }
}

void pm_metal_net_ssh_pkt_reset(void)
{
    g_acc_len = 0;
    g_encrypted = 0;
    g_seq_out = 0;
    g_seq_in = 0;
    memset(g_key_c2s, 0, sizeof(g_key_c2s));
    memset(g_key_s2c, 0, sizeof(g_key_s2c));
}

void pm_metal_net_ssh_pkt_set_keys(const uint8_t key_c2s[64],
                                   const uint8_t key_s2c[64])
{
    memcpy(g_key_c2s, key_c2s, 64);
    memcpy(g_key_s2c, key_s2c, 64);
    g_encrypted = 1;
    g_seq_out = 0;
    g_seq_in = 0;
}

static void chacha_poly_keys(const uint8_t key[64], uint32_t seq, uint8_t poly[32],
                             uint8_t *block)
{
    uint8_t nonce[8];
    uint8_t zeros[64];

    put_u64_be(nonce, (uint64_t)seq);
    memset(zeros, 0, sizeof(zeros));
    /* OpenSSH: main=key[0:32] (payload+poly), header=key[32:64] (length). */
    (void)crypto_chacha20_djb(block, zeros, 64, key, nonce, 0);
    memcpy(poly, block, 32);
}

int32_t pm_metal_net_ssh_pkt_push(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0u) {
        return 0;
    }
    if (g_acc_len + len > sizeof(g_acc)) {
        g_acc_len = 0;
        return -1;
    }
    memcpy(g_acc + g_acc_len, data, len);
    g_acc_len += len;
    return 0;
}

static int32_t send_clear(const uint8_t *payload, uint32_t payload_len)
{
    uint8_t pkt[PM_METAL_SSH_PKT_ACC];
    uint32_t pad;
    uint32_t pkt_len;
    uint32_t i;

    if (payload == NULL || payload_len == 0u || payload_len > 3500u) {
        return -1;
    }
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
    return sock_send(pkt, 4u + pkt_len);
}

static int32_t send_enc(const uint8_t *payload, uint32_t payload_len)
{
    uint8_t plain[PM_METAL_SSH_PKT_ACC];
    uint8_t out[PM_METAL_SSH_PKT_ACC];
    uint8_t nonce[8];
    uint8_t poly_key[32];
    uint8_t block[64];
    uint8_t mac[16];
    uint32_t pad;
    uint32_t body_len;
    uint32_t i;

    if (payload == NULL || payload_len == 0u || payload_len > 3000u) {
        return -1;
    }
    /* AEAD: pad so (1+payload+pad) % 8 == 0, pad >= 4 */
    pad = (8u - ((payload_len + 1u) % 8u)) % 8u;
    if (pad < 4u) {
        pad += 8u;
    }
    body_len = 1u + payload_len + pad;
    if (4u + body_len + 16u > sizeof(out)) {
        return -1;
    }
    plain[0] = (uint8_t)pad;
    memcpy(plain + 1, payload, payload_len);
    for (i = 0; i < pad; i++) {
        plain[1u + payload_len + i] = (uint8_t)(0x5au ^ (uint8_t)i);
    }
    put_u32(out, body_len);
    put_u64_be(nonce, (uint64_t)g_seq_out);
    /* Length with header key; payload with main key (OpenSSH layout). */
    (void)crypto_chacha20_djb(out, out, 4, g_key_s2c + 32, nonce, 0);
    chacha_poly_keys(g_key_s2c, g_seq_out, poly_key, block);
    (void)crypto_chacha20_djb(out + 4, plain, body_len, g_key_s2c, nonce, 1);
    crypto_poly1305(mac, out, 4u + body_len, poly_key);
    memcpy(out + 4 + body_len, mac, 16);
    g_seq_out++;
    return sock_send(out, 4u + body_len + 16u);
}

int32_t pm_metal_net_ssh_pkt_send(const uint8_t *payload, uint32_t payload_len)
{
    if (g_encrypted) {
        return send_enc(payload, payload_len);
    }
    return send_clear(payload, payload_len);
}

static int32_t recv_clear(uint8_t *payload, uint32_t cap, uint32_t *len_out)
{
    uint8_t chunk[256];
    uint32_t n = 0;
    uint32_t pkt_len;
    uint32_t need;
    uint8_t pad;
    uint32_t plen;
    int32_t rc;

    *len_out = 0;
    rc = sock_recv(chunk, sizeof(chunk), &n);
    if (rc == 1 && n > 0u) {
        if (g_acc_len + n > sizeof(g_acc)) {
            g_acc_len = 0;
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
        g_acc_len = 0;
        return -1;
    }
    need = 4u + pkt_len;
    if (g_acc_len < need) {
        return 0;
    }
    pad = g_acc[4];
    if (pad < 4u || (uint32_t)pad + 1u >= pkt_len) {
        g_acc_len = 0;
        return -1;
    }
    plen = pkt_len - 1u - (uint32_t)pad;
    if (plen > cap) {
        g_acc_len = 0;
        return -1;
    }
    memcpy(payload, g_acc + 5, plen);
    *len_out = plen;
    memmove(g_acc, g_acc + need, g_acc_len - need);
    g_acc_len -= need;
    return 1;
}

static int32_t recv_enc(uint8_t *payload, uint32_t cap, uint32_t *len_out)
{
    uint8_t chunk[256];
    uint8_t nonce[8];
    uint8_t poly_key[32];
    uint8_t block[64];
    uint8_t mac[16];
    uint8_t lenb[4];
    uint32_t n = 0;
    uint32_t body_len;
    uint32_t need;
    uint8_t pad;
    uint32_t plen;
    int32_t rc;

    *len_out = 0;
    rc = sock_recv(chunk, sizeof(chunk), &n);
    if (rc == 1 && n > 0u) {
        if (g_acc_len + n > sizeof(g_acc)) {
            g_acc_len = 0;
            return -1;
        }
        memcpy(g_acc + g_acc_len, chunk, n);
        g_acc_len += n;
    }
    if (g_acc_len < 4u) {
        return 0;
    }
    put_u64_be(nonce, (uint64_t)g_seq_in);
    memcpy(lenb, g_acc, 4);
    (void)crypto_chacha20_djb(lenb, lenb, 4, g_key_c2s + 32, nonce, 0);
    body_len = get_u32(lenb);
    if (body_len < 5u || body_len > sizeof(g_acc) - 20u) {
        g_acc_len = 0;
        return -1;
    }
    need = 4u + body_len + 16u;
    if (g_acc_len < need) {
        return 0;
    }
    chacha_poly_keys(g_key_c2s, g_seq_in, poly_key, block);
    crypto_poly1305(mac, g_acc, 4u + body_len, poly_key);
    if (crypto_verify16(mac, g_acc + 4 + body_len) != 0) {
        g_acc_len = 0;
        return -1;
    }
    (void)crypto_chacha20_djb(g_acc + 4, g_acc + 4, body_len, g_key_c2s, nonce, 1);
    pad = g_acc[4];
    if (pad < 4u || (uint32_t)pad + 1u >= body_len) {
        g_acc_len = 0;
        return -1;
    }
    plen = body_len - 1u - (uint32_t)pad;
    if (plen > cap) {
        g_acc_len = 0;
        return -1;
    }
    memcpy(payload, g_acc + 5, plen);
    *len_out = plen;
    memmove(g_acc, g_acc + need, g_acc_len - need);
    g_acc_len -= need;
    g_seq_in++;
    return 1;
}

int32_t pm_metal_net_ssh_pkt_recv(uint8_t *payload, uint32_t cap, uint32_t *len_out)
{
    if (payload == NULL || len_out == NULL) {
        return -1;
    }
    if (g_encrypted) {
        return recv_enc(payload, cap, len_out);
    }
    return recv_clear(payload, cap, len_out);
}
