#include "crypto.h"

#include <string.h>

#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "sha256.h"

static uint8_t g_host_sk[64];
static uint8_t g_host_pk[32];
static uint32_t g_rng;

static uint32_t rdtsc32(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    (void)hi;
    return lo;
}

int32_t pm_metal_net_ssh_crypto_init(void)
{
    uint8_t seed[32];

    g_rng = rdtsc32() ^ 0x6d657461u;
    pm_metal_net_ssh_crypto_random(seed, sizeof(seed));
    crypto_ed25519_key_pair(g_host_sk, g_host_pk, seed);
    crypto_wipe(seed, sizeof(seed));
    return 0;
}

void pm_metal_net_ssh_crypto_random(uint8_t *out, size_t n)
{
    size_t i;

    if (out == NULL) {
        return;
    }
    for (i = 0; i < n; i++) {
        g_rng = g_rng * 1664525u + 1013904223u + rdtsc32();
        out[i] = (uint8_t)(g_rng >> 16);
    }
}

const uint8_t *pm_metal_net_ssh_host_ed25519_pk(void)
{
    return g_host_pk;
}

const uint8_t *pm_metal_net_ssh_host_ed25519_sk(void)
{
    return g_host_sk;
}

void pm_metal_net_ssh_sha256(const uint8_t *data, size_t n, uint8_t out[32])
{
    CRYAL_SHA256_CTX ctx;

    sha256_init(&ctx);
    if (data != NULL && n > 0u) {
        sha256_update(&ctx, data, n);
    }
    sha256_final(&ctx, out);
}

void pm_metal_net_ssh_sha256_init(void *ctx)
{
    sha256_init((CRYAL_SHA256_CTX *)ctx);
}

void pm_metal_net_ssh_sha256_update(void *ctx, const uint8_t *data, size_t n)
{
    if (data != NULL && n > 0u) {
        sha256_update((CRYAL_SHA256_CTX *)ctx, data, n);
    }
}

void pm_metal_net_ssh_sha256_final(void *ctx, uint8_t out[32])
{
    sha256_final((CRYAL_SHA256_CTX *)ctx, out);
}

size_t pm_metal_net_ssh_sha256_ctx_size(void)
{
    return sizeof(CRYAL_SHA256_CTX);
}

void pm_metal_net_ssh_x25519_keypair(uint8_t sk[32], uint8_t pk[32])
{
    pm_metal_net_ssh_crypto_random(sk, 32);
    crypto_x25519_public_key(pk, sk);
}

void pm_metal_net_ssh_x25519(uint8_t shared[32], const uint8_t sk[32],
    const uint8_t peer_pk[32])
{
    crypto_x25519(shared, sk, peer_pk);
}

void pm_metal_net_ssh_ed25519_sign(uint8_t sig[64], const uint8_t *msg,
    size_t msg_len)
{
    crypto_ed25519_sign(sig, g_host_sk, msg, msg_len);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

uint32_t pm_metal_net_ssh_put_string(uint8_t *dst, uint32_t cap, const void *s,
    uint32_t n)
{
    if (dst == NULL || 4u + n > cap) {
        return 0;
    }
    put_u32(dst, n);
    if (n > 0u && s != NULL) {
        memcpy(dst + 4, s, n);
    }
    return 4u + n;
}

uint32_t pm_metal_net_ssh_put_cstring(uint8_t *dst, uint32_t cap, const char *s)
{
    uint32_t n = 0;

    if (s != NULL) {
        while (s[n] != '\0') {
            n++;
        }
    }
    return pm_metal_net_ssh_put_string(dst, cap, s, n);
}

uint32_t pm_metal_net_ssh_put_mpint(uint8_t *dst, uint32_t cap,
    const uint8_t *be, uint32_t n)
{
    uint32_t skip = 0;
    uint32_t body;
    int need_zero;

    if (dst == NULL || be == NULL) {
        return 0;
    }
    while (skip + 1u < n && be[skip] == 0u) {
        skip++;
    }
    body = n - skip;
    need_zero = (body > 0u && (be[skip] & 0x80u) != 0u) ? 1 : 0;
    if (4u + (uint32_t)need_zero + body > cap) {
        return 0;
    }
    put_u32(dst, (uint32_t)need_zero + body);
    if (need_zero) {
        dst[4] = 0;
        memcpy(dst + 5, be + skip, body);
    } else if (body > 0u) {
        memcpy(dst + 4, be + skip, body);
    }
    return 4u + (uint32_t)need_zero + body;
}

uint32_t pm_metal_net_ssh_hostkey_blob(uint8_t *dst, uint32_t cap)
{
    uint32_t o = 0;
    uint32_t n;

    n = pm_metal_net_ssh_put_cstring(dst + o, cap - o, "ssh-ed25519");
    if (n == 0u) {
        return 0;
    }
    o += n;
    n = pm_metal_net_ssh_put_string(dst + o, cap - o, g_host_pk, 32);
    if (n == 0u) {
        return 0;
    }
    return o + n;
}
